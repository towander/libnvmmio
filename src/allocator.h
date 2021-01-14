#ifndef LIBNVMMIO_ALLOCATOR_H
#define LIBNVMMIO_ALLOCATOR_H

#include <pthread.h>

#include "lock.h"
#include "mmio.h"
#include "radixlog.h"
#include "slist.h"

typedef struct freelist_node_struct {
  struct slist_head list;
  struct slist_head skip;
  void *ptr;
} fnode_t;

typedef struct freelist_struct {
  pthread_mutex_t mutex;
  struct slist_head list_head;
  struct slist_head skip_head;
  unsigned long list_cnt;
  unsigned long skip_cnt;
  unsigned long skip_unit;
} flist_t;

void init_allocator(void);

mmio_t *get_new_mmio(int fd, int flags, unsigned long ino, unsigned long fsize);
void release_mmio(mmio_t *mmio, int flags, int fd);

log_table_t *alloc_log_table(table_type_t type);
void free_log_table(log_table_t *table);

idx_entry_t *alloc_idx_entry(log_size_t log_size);
void free_idx_entry(idx_entry_t *entry, log_size_t log_size);

void *alloc_log_data(log_size_t log_size);
void free_log_data(void *data, log_size_t log_size);

flist_t *alloc_flist(unsigned long skip_unit);
fnode_t *get_fnode(void);
void put_fnode(fnode_t *node);

void put_global(fnode_t *node, flist_t *global);
void fill_local_provider(flist_t *local, flist_t *global);
void put_local_collector(fnode_t *node, flist_t *local_collector,
                         flist_t *global);

/*
 * Before calling PUSH_GLOBAL(), global->mutex must be acquired.
 */
#define PUSH_GLOBAL(obj_, global_) \
  do {                             \
    fnode_t *node_;                \
    node_ = get_fnode();           \
    node_->ptr = obj_;             \
    put_global(node_, global_);    \
  } while (0)

#define POP_PROVIDER(obj_, type_, provider_, global_)                     \
  do {                                                                    \
    fnode_t *node_;                                                       \
    if (__glibc_unlikely(provider_ == NULL)) {                            \
      provider_ = alloc_flist(0);                                         \
    }                                                                     \
    if (__glibc_unlikely(slist_empty(&provider_->list_head))) {           \
      fill_local_provider(provider_, global_);                            \
    }                                                                     \
    node_ = SLIST_ENTRY(slist_pop(&provider_->list_head), fnode_t, list); \
    obj_ = (type_ *)node_->ptr;                                           \
    put_fnode(node_);                                                     \
  } while (0)

#define PUSH_COLLECTOR(obj_, collector_, global_)    \
  do {                                               \
    fnode_t *node_;                                  \
    if (__glibc_unlikely(collector_ == NULL)) {      \
      collector_ = alloc_flist(global_->skip_unit);  \
    }                                                \
    node_ = get_fnode();                             \
    node_->ptr = obj_;                               \
    put_local_collector(node_, collector_, global_); \
  } while (0)

#endif /* LIBNVMMIO_ALLOCATOR_H */
