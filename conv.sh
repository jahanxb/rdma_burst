#!/bin/bash 
filename='cpu_stat.txt'
n=2
while read line; do
echo "line changed: $n " ;
tail -n $n cpu_stat.txt  | sed -n '8, 12{s/^ *//;s/ *$//;s/  */,/gp;};12q' >> out.txt
n=$((n+1))
done < $filename
