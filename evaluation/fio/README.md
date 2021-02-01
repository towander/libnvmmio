# FIO
[FIO](https://github.com/axboe/fio) is a popular benchmark for measuring file I/O performance.
You can measure the performance of Libnvmmio using FIO.

## Install FIO

```bash
$ ./install.sh
```
You can prepare FIO experiments by running the [install.sh](https://github.com/chjs/libnvmmio/blob/main/evaluation/fio/install.sh) script.
1. This script clones the latest version of [FIO](https://github.com/axboe/fio) and changes it to the version we tested (```2ef3c1b02473a14bf7b8b52e28d0cdded9c5cc9a```).
That version is currently the most recent (Feb. 1, 2021).
2. Then, the script modifies the source code of FIO to use Libnvmmio.
As the [fio-libnvmmio.patch](https://github.com/chjs/libnvmmio/blob/main/evaluation/fio/fio-libnvmmio.patch) file shows, FIO needs to modify only 3 lines of source code.
3. Finally, the script builds FIO.

## Mount a NVMM File System
The [scripts](https://github.com/chjs/libnvmmio/tree/main/scripts) directory contains scripts for mounting on some filesystems.
```bash
$ ls ../../scripts/
dax_config.sh  nova_config.sh  pmfs_config.sh
```

You can use the script files to mount a filesystem you want.
```bash
$ ../../scripts/dax_config.sh 
[sudo] password for chjs: 
performance
mke2fs 1.45.6 (20-Mar-2020)
/dev/pmem0 contains a ext4 file system
        last mounted on /mnt/pmem on Mon Feb  1 14:23:16 2021
Creating filesystem with 25165824 4k blocks and 6291456 inodes
Filesystem UUID: 92fabccd-fa4c-4555-9aa7-387a565c62cd
Superblock backups stored on blocks: 
        32768, 98304, 163840, 229376, 294912, 819200, 884736, 1605632, 2654208, 
        4096000, 7962624, 11239424, 20480000, 23887872

Allocating group tables: done                            
Writing inode tables: done                            
Creating journal (131072 blocks): done
Writing superblocks and filesystem accounting information: done
```

## Build Libnvmmio
Libnvmmio source codes and the makefile are in the [src](https://github.com/chjs/libnvmmio/tree/main/src) directory.
```bash
$ make -C ../../src/
```

## Run FIO using Libnvmmio
```bash
$ ./run.sh
```
This [run.sh](https://github.com/chjs/libnvmmio/blob/main/evaluation/fio/run.sh) script runs FIO using ```LD_PRELOAD``` to intercept the calls during runtime and forward them to Libnvmmio.
