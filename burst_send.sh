#!/bin/bash
for i in {1..1}
do
echo "Running process: $i";
sudo ./rxfer_test -c 10.0.0.50 -i 1 -t 20 -f /data/jahanxb/10MB10000 -o 32 -a 4 -p 5201 -r  -q 18151 &
sudo ./rxfer_test -c 10.0.0.50 -i 1 -t 20 -f /data/jahanxb/10MB10000 -o 32 -a 4 -p 5202 -r -q 18152
done