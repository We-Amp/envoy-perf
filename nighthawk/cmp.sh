#/bin/bash

con=$1
rps=$2

taskset -c 0 ~/Code/wrk2/wrk -c $1 -d 3s -t 1 -U -R $2 http://127.0.0.1:10000/ 2>&1 | egrep -i "(\#)|(requests)" | tail +4
taskset -c 0 bazel-bin/nighthawk_client --concurrency auto --rps $2 --connections $1 --duration 3 http://127.0.0.1:10000/ 
#&& tools/stats.py res.txt benchmark
