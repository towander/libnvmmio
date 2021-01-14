# Libnvmmio

[Libnvmmio](https://github.com/chjs/libnvmmio) rebuilds performance-critical software IO path to reduce software overhead in non-volatile main memory (NVMM) systems.
Libnvmmio is a simple and practical solution, which provides low-latency and scalable file IO while guaranteeing data atomicity.
It leverages the memory-mapped IO for fast data access and makes applications free from the crash-consistency concerns by providing failure-atomicity.

Libnvmmio runs in the address space of a target application as a library and interacts with underlying file systems.
Libnvmmio intercepts IO requests and turns them into internal operations.
For each IO request, Libnvmmio distinguishes data and metadata operations.
For all data requests, Libnvmmio services them in the user-level library, bypassing the slow kernel code.
Meanwhile, for complex metadata and directory operations, Libnvmmio passes them to the kernel.
This design is based on the observation that data updates are frequently-executed, performance-critical operations.
On the other hand, the metadata and directory operations are infrequently executed and envolve complex implementation to support POSIX semantics.
Handling them differently, the architecture of Libnvmmio follows the design principle of making the common case fast with a simple, fast user-level implementation.

We presented Libnvmmio at the [USENIX ATC '20](https://www.usenix.org/conference/atc20/presentation/choi) conference.
If you need more details, please refer to [our paper](https://www.usenix.org/system/files/atc20-choi.pdf).

## System Requirements
* **NVMM File Systems**.
Libnvmmio runs on any NVMM file systems that provide DAX-mmap.
We tested Libnvmmio on various underlying file systems (_e.g._, [Ext4-DAX](https://www.kernel.org/doc/Documentation/filesystems/dax.txt), [PMFS](https://github.com/linux-pmfs/pmfs), [NOVA](https://github.com/NVSL/linux-nova), [SplitFS](https://github.com/utsaslab/SplitFS)).

* **Persistent Memory Development Kit (PMDK)**.
Libnvmmio uses the [PMDK](https://pmem.io/pmdk/) library to write data to NVM.
When writing data through non-temporal stores, it uses ```pmem_memcpy_nodrain``` and ```pmem_drain```.
It also uses ```pmem_flush``` to make metadata updates permanent.
PMDK provides optimizations for data-level parallelism with SIMD instructions.
It also plans to support ARM processors as well as Intel/AMD processors.

## Contents
* ```src/``` contains source code for Libnvmmio.
* ```scripts/``` contains shell scripts to mount kernel filesystems.
* ```evaluation/``` contains applications and descriptions that evaluate Libnvmmio.

## Current Limitations
* **Limited file IO APIs**.
The current implementation of Libnvmmio handles the following system calls.
  * ```open, close, read, write, fsync, lseek```
  * We pass the rest of the calls to the underlying kernel filesystem.
We will continue to add more file IO APIs to Libnvmmio.

* **Journal wrap**.
Libnvmmio keeps updated data in its logs and atomically writes to the original file when ```fsync``` called.
If a journal wrap occurs, however, failure-atomicity cannot be guaranteed.
When the log size reaches the threshold, Libnvmmio forces committing and checkpointing the logged data.
Since journal wrap may occur in all journaling systems, journal space should be large enough to handle IO requests.
In Libnvmmio, the threshold is configurable at compile time.

* **File sharing between multiple processes**.
Currently, Libnvmmio doesn’t allow file sharing.
So, we need a coordination technique between concurrent accesses from multiple processes.
A lease mechanism, as in [Strata](https://github.com/ut-osa/strata), can be adopted in Libnvmmio.

## Acknowledgments
This work was supported in part by Samsung Electronics and the National Research Foundation in Korea under PF Class Heterogeneous High-Performance Computer Development (NRF-2016M3C4A7952587).

## Contact
Please contact us at ```chjs@skku.edu``` with any questions.
