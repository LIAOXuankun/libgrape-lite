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

#ifndef EXAMPLES_ANALYTICAL_APPS_GET_INCORENESS_H_
#define EXAMPLES_ANALYTICAL_APPS_GET_INCORENESS_H_

#include <grape/grape.h>

#include "dcore/get_incoreness_context.h"

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
class GetInCoreness : public ParallelAppBase<FRAG_T, GetInCorenessContext<FRAG_T>>,
public ParallelEngine {
public:
// specialize the templated worker.
INSTALL_PARALLEL_WORKER(GetInCoreness<FRAG_T>, GetInCorenessContext<FRAG_T>, FRAG_T)
using vertex_t = typename fragment_t::vertex_t;
static constexpr MessageStrategy message_strategy =
    MessageStrategy::kAlongOutgoingEdgeToOuterVertex;
static constexpr LoadStrategy load_strategy = LoadStrategy::kBothOutIn;

bool HIndex(const fragment_t& frag, context_t& ctx, const vertex_t& v){
  auto& h_vec = ctx.h_neighbor_value[v];
  h_vec.clear();
  auto es = frag.GetIncomingAdjList(v);
  for (auto& e : es) {
    vertex_t u = e.get_neighbor();
    h_vec.push_back(ctx.partial_result[u]);
  }
  sort(h_vec.begin(), h_vec.end(), std::greater<int>());
  int h = 0;
  for(int i = 0; i < static_cast<int>(h_vec.size()); i++){
    if(h_vec[i] >= i + 1)
      h = i + 1;
  }
  if (ctx.partial_result[v] > h) {
    ctx.partial_result[v] = h;
    return true;
  }
  return false;
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
//      auto n_inner_vertices = frag.GetInnerVerticesNum();
//      auto n_outer_vertices = frag.GetOuterVerticesNum();

  auto inner_vertices = frag.InnerVertices();
  auto outer_vertices = frag.OuterVertices();

  messages.InitChannels(thread_num());
//      std::cerr << n_inner_vertices << "\t" << n_outer_vertices << std::endl;

#ifdef PROFILING
  ctx.exec_time -= GetCurrentTime();
#endif
  //ctx.next_modified.ParallelClear(thread_num());

  // Get the channel. Messages assigned to this channel will be sent by the
  // message manager in parallel with the evaluation process.
  // auto& channel_0 = messages.Channels()[0];
  for(auto v: inner_vertices){
    ctx.partial_result[v] = frag.GetLocalInDegree(v);
  }

#ifdef PROFILING
  ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif
  for(auto v: inner_vertices){
    messages.SendMsgThroughOEdges<fragment_t, int>(frag, v,
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

  //ctx.next_modified.ParallelClear(thread_num());


  // parallel process and reduce the received messages
  messages.ParallelProcess<fragment_t, int>(
      thread_num(), frag, [&ctx](int tid, vertex_t u, int msg) {
        if (ctx.partial_result[u] > msg) {
          ctx.partial_result[u] = msg;
        }
      });

//    std::cerr << "Enter refining the h_index" << std::endl;
  for(auto v: inner_vertices){
    if(HIndex(frag, ctx, v)) {
      messages.SendMsgThroughOEdges<fragment_t, int>(frag, v,
                                                     ctx.partial_result[v]);
    }

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

#endif  // EXAMPLES_ANALYTICAL_APPS_GET_INCORENESS_H_