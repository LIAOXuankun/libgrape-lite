echo "outcoreness computation:"
mpirun -n 4 ./run_app --vfile ../dataset/email.v --efile ../dataset/email.e --application getoutcoreness --out_prefix ./output_outcoreness --directed
echo "incoreness computation:"
mpirun -n 4 ./run_app --vfile ../dataset/email.v --efile ../dataset/email.e --application getincoreness --out_prefix ./output_incoreness --directed
cat  ./output_outcoreness/result_frag_* &> ./output_outcoreness/outcoreness.v
cat  ./output_incoreness/result_frag_* &> ./output_incoreness/incoreness.v
awk '{print $2}' ./output_outcoreness/outcoreness.v &> ./output_outcoreness/pure_outcoreness.v
paste -d. ./output_incoreness/incoreness.v ./output_outcoreness/pure_outcoreness.v &> ./output_incoreness/email_SH.v
echo "skyline computation:"
mpirun -n 4 ./run_app --vfile ./output_incoreness/email_SH.v --efile ../dataset/email.e --application dcoreoptimized --out_prefix ./output_dcoreoptimized --directed
cat ./output_dcoreoptimized/result_frag_* &> ./output_dcoreoptimized/result.txt

