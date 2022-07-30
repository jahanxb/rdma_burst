#!/bin/sh
sudo ./rxfer_test -c 10.0.0.50 -i 1 -t 10 -f /data/jahanxb/10MB10000 -p 5201 -a 4 -o 28 -r &
sudo ./cpu_usage.sh rxfer_test > temp.txt