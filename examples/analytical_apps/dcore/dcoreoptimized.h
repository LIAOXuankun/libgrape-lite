/** Copyright 2020 Alibaba Group Holding Limited.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef EXAMPLES_ANALYTICAL_APPS_DCORE_DCOREOPTIMIZED_H_
#define EXAMPLES_ANALYTICAL_APPS_DCORE_DCOREOPTIMIZED_H_

#include <grape/grape.h>

#include "dcore/dcore_optimized_context.h"

namespace grape {

/**
 * @brief DCoreFirst application, determines the length of the shortest paths from a
 * given source vertex to all other vertices in graphs, which can work
 * on both directed and undirected graph.
 *
 * This version of DCoreFirst inherits ParallelAppBase. Messages can be sent in
 * parallel with the evaluation process. This strategy improves the performance
 * by overlapping the communication time and the evaluation time.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T>
class DcoreOptimized : public ParallelAppBase<FRAG_T, DCoreOptimizedContext<FRAG_T>>,
public ParallelEngine {
public:
// specialize the templated worker.
INSTALL_PARALLEL_WORKER(DcoreOptimized<FRAG_T>, DCoreOptimizedContext<FRAG_T>, FRAG_T)
using vertex_t = typename fragment_t::vertex_t;
static constexpr MessageStrategy message_strategy =
    MessageStrategy::kAlongEdgeToOuterVertex;
static constexpr LoadStrategy load_strategy = LoadStrategy::kBothOutIn;

bool BiSearch(std::vector<int> &NeighborSkyline, int &k, int &l){    //given (k,l)-core, whether domaining pairs exsists in vec
  bool exist = false;
  int len = NeighborSkyline.size() / 2 ;
  if(NeighborSkyline[len*2-2] < k){
    return exist;
  }


  int first = 0;
  int half , middle;

  while(len > 0){
    half = len >> 1 ;
    middle = first + half;
    if(NeighborSkyline[2*middle] >= k)
      len = half;
    else{
      first = middle +1;
      len = len - half - 1;
    }

  }
  if(NeighborSkyline[2*first+1] >= l)
    exist = true;
  else
    exist = false;

  return exist;

}


bool NoVerify(const fragment_t& frag, context_t& ctx, const vertex_t& v) {
  bool update = false;
  int OutMax = ctx.partial_result[v][1];
  int InMax = ctx.partial_result[v][ctx.partial_result[v].size()-2];

  std::vector <int> result;
  for(int k = 0; k <= InMax; k++){
    for(int l = OutMax; l >= 0; l--){
      int in = 0;
      int out = 0;
      auto es_in = frag.GetIncomingAdjList(v);
      for (auto& e : es_in) {
        vertex_t u = e.get_neighbor();
        if(BiSearch(ctx.partial_result[u] , k , l)){
          in++;
        }
        if(in >= k) break;

      }

      auto es_out = frag.GetOutgoingAdjList(v);
      for(auto& e : es_out) {
        vertex_t u = e.get_neighbor();
        if(BiSearch(ctx.partial_result[u] , k , l)){
          out++;
        }
        if(out >= l) break;
      }

      if(in >= k && out >= l){
        int VecSize = result.size();

        if(VecSize == 0 || l!=result[VecSize-1]){
          result.push_back(k);
          result.push_back(l);
          OutMax = l;
          break;
        }
        else{
          result[VecSize-2] = k;
          break;
        }
      }
    }
  }

  if(result != ctx.partial_result[v]){
    update = true;
    ctx.cache_result[v] = result;
  }
  return update;

}



/**
 * @brief Partial evaluation for DCore.
 *
 * @param frag
 * @param ctx
 * @param messages
 */

void PEval(const fragment_t& frag, context_t& ctx,
           message_manager_t& messages) {

  auto inner_vertices = frag.InnerVertices();
  auto outer_vertices = frag.OuterVertices();

  messages.InitChannels(thread_num());
//      std::cerr  << frag.fid() << n_inner_vertices << "\t" << n_outer_vertices << std::endl;

#ifdef PROFILING
  ctx.exec_time -= GetCurrentTime();
#endif


  // Get the channel. Messages assigned to this channel will be sent by the
  // message manager in parallel with the evaluation process.
  // auto& channel_0 = messages.Channels()[0];
  for(auto v: inner_vertices){
    std::string tmp =frag.GetData(v);
    std::regex ws_re("\\.");
    std::vector<std::string> core(std::sregex_token_iterator(tmp.begin(), tmp.end(), ws_re, -1),std::sregex_token_iterator());
    for (unsigned i = 0; i < core.size(); i++){
      ctx.partial_result[v].push_back(std::stoi(core[i]));
      ctx.cache_result[v].push_back(std::stoi(core[i]));
    }
  }

#ifdef PROFILING
  ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif
  for(auto v: inner_vertices){
    messages.SendMsgThroughEdges<fragment_t, std::vector<int>>(frag, v,
                                                               ctx.partial_result[v]);
  }


#ifdef PROFILING
  ctx.postprocess_time += GetCurrentTime();
#endif
}

/**
 * @brief Incremental evaluation for SSSP.
 *
 * @param frag
 * @param ctx
 * @param messages
 */
void IncEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
  auto inner_vertices = frag.InnerVertices();
  auto outer_vertices = frag.OuterVertices();

#ifdef PROFILING
  ctx.preprocess_time -= GetCurrentTime();
#endif
  // parallel process and reduce the received message

  messages.ParallelProcess<fragment_t, std::vector<int>>(
      thread_num(), frag, [&ctx](int tid, vertex_t u, std::vector<int> msg) {


        for(int i = 0; i < static_cast<int>(msg.size()); i++) {
          if((msg.size()!= ctx.partial_result[u].size() || (ctx.partial_result[u].empty())||(ctx.partial_result[u][i] != msg[i]))) {
            ctx.partial_result[u] = msg;
            break;
          }
        }
      });


  for (auto v: inner_vertices) {
    if (NoVerify(frag, ctx, v)) {
      messages.SendMsgThroughEdges<fragment_t, std::vector<int>>(frag, v,
                                                                 ctx.cache_result[v]);
    }
  }


  for (auto v: inner_vertices) {
    ctx.partial_result[v] = ctx.cache_result[v];

  }




#ifdef PROFILING
  ctx.preprocess_time += GetCurrentTime();
    ctx.exec_time -= GetCurrentTime();
#endif

  // incremental evaluation.

  // put messages into channels corresponding to the destination fragments.

#ifdef PROFILING
  ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif


#ifdef PROFILING
  ctx.postprocess_time += GetCurrentTime();
#endif

}
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_DCORE_DCORESECOND_H_