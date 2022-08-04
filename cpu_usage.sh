#!/bin/sh

while true
do 
top -b -n 1 -p $(pidof "$@" |sed s#\ #,#g) 2>/dev/null | tail -n 1 
if [ $? -ne 0 ]; then
  echo No processes with the specified name\(s\) were found
fi
#/bin/bash -c "top -n 1 | grep  'rxfer_test' >> temp.txt";
done