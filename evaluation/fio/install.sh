#!/bin/bash
git clone https://github.com/axboe/fio.git src
cd src
git checkout 2ef3c1b02473a14bf7b8b52e28d0cdded9c5cc9a
patch -p1 < ../fio-libnvmmio.patch
./configure && make -j 16

