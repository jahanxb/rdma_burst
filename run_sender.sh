#!/bin/sh

log_cpu_stats(){
sudo ./cpu_usage.sh rxfer_test >> temp.txt
}


$i = 1;
for j in {1...10000}
do
log_cpu_stats
done

wait
echo "Process Completed.. Check Logs"