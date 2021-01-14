# Building Libnvmmio

To install PMDK on Ubuntu 18.04 or later
```bash
$ sudo apt install libpmem-dev
```

To build Libnvmmio
```bash
$ cd libnvmmio/src
$ make
```

# Configuration
The configurable options of Libnvmmio can be set in the [config.h](https://github.com/chjs/libnvmmio/blob/master/src/config.h) file.

## Default Logging Policy
Libnvmmio uses hybrid logging.
It dynamically sets the appropriate logging policy (undo or redo) each time ```fsync()``` is called.
You can set the default policy to use before ```fsync()``` is called with the ```DEFAULT_POLICY``` variable.

```c
#if 1
#define DEFAULT_POLICY UNDO
#else
#define DEFAULT_POLICY REDO
#endif
```

If you do not want to use hybrid logging, define the ```HYBRID_LOGGING``` variable to ```false```.
When hybrid logging is off, Libnvmmio uses only the default logging policy.

```c
#define HYBRID_LOGGING true
```

## Various Log Sizes
Libnvmmio uses per-block logging.
The size of each per-block log can be varied according to the IO size (4KB~2MB).
If you only need the 4KB log size, comment out the other sizes as below.
```c
typedef enum log_size_enum {
  LOG_4K,
  /*
  LOG_8K,
  LOG_16K,
  LOG_32K,
  LOG_64K,
  LOG_128K,
  LOG_256K,
  LOG_512K,
  LOG_1M,
  LOG_2M,
  */
  NR_LOG_SIZES
} log_size_t;
```

## Log File Size
The size of the log file in which logs are stored is defined by the ```LOG_FILE_SIZE``` variable.
To avoid journal wrap, you should set the appropriate log file size according to the application characteristics.
In the current implementation, the amount of memory that Libnvmmio uses for logging is ```NR_LOG_SIZES``` * ```LOG_FILE_SIZE```.
```c
#define LOG_FILE_SIZE (1UL << 32)   /* 4GB */
```

## PMEM Path
To store log files you need to set the path where the NVMM filesystem is mounted.
```c
#define DEFAULT_PMEM_PATH "/mnt/pmem"
```

Alternatively, you can set the ```PMEM_PATH``` variable when running an application as follows.
```bash
$ LD_PRELOAD=/path/to/libnvmmio.so PMEM_PATH=/path/to/pmem ./a.out
```
