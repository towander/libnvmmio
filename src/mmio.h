#ifndef LIBNVMMIO_MMAP_H
#define LIBNVMMIO_MMAP_H

#include <libpmem.h>
#include <pthread.h>

#include "radixlog.h"
#include "slist.h"

typedef enum { UNDO, REDO } policy_t;

typedef struct mmio_struct {
  pthread_rwlock_t rwlock;
  void *start;
  void *end;
  unsigned long ino;
  unsigned long offset;
  unsigned long epoch;
  policy_t policy;
  radix_root_t radixlog;
  unsigned long read;
  unsigned long write;
  off_t fsize;
  pthread_t checkpoint_thread;
  int ref;
} mmio_t;

#define NTSTORE(dst, src, n) pmem_memcpy_nodrain(dst, src, n)
#define FENCE() pmem_drain();
#define FLUSH(addr, n) pmem_flush(addr, n);

ssize_t read_redolog(struct slist_head *entries_head, void *dst,
                     void *file_addr, unsigned long offset, unsigned long len);
ssize_t mmio_read(mmio_t *mmio, off_t offset, void *buf, size_t len);
ssize_t mmio_write(mmio_t *mmio, int fd, off_t offset, const void *buf,
                   off_t len);

void create_checkpoint_thread(mmio_t *mmio);
void checkpoint_mmio(mmio_t *mmio);
void commit_mmio(mmio_t *mmio);

#endif /* LIBNVMMIO_MMAP_H */
