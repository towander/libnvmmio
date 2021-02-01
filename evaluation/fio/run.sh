#!/bin/bash
LD_PRELOAD=../../src/libnvmmio.so \
PMEM_PATH=/mnt/pmem \
numactl --cpunodebind=0 --membind=0 \
src/fio \
--name=test \
--ioengine=sync \
--rw=write \
--directory=/mnt/pmem \
--filesize=128m \
--bs=4k \
--thread --numjobs=16 \
--runtime=10 --time_based

