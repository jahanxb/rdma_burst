#!/bin/bash
for i in {1..1}
do
echo "Running process: $i";
sudo ./rxfer_test -s -r -f /data/jahanxb/recv_data/fl1 -p 5201 -q 18151 &
sudo ./rxfer_test -s -r -f /data/jahanxb/recv_data/fl2 -p 5202 -q 18152
done