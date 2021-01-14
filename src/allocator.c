#include "allocator.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "config.h"
#include "debug.h"
#include "file.h"
#include "lock.h"
#include "mmio.h"
#include "radixlog.h"
#include "slist.h"

#define DIR_PATH "%s/.libnvmmio-%lu"

extern struct fops_struct posix;
static char logdir_path[128];
static unsigned long libnvmmio_pid;

static flist_t *global_log_list[NR_LOG_SIZES] = {
    NULL,
};
static __thread flist_t *local_log_provider[NR_LOG_SIZES] = {
    NULL,
};
static __thread flist_t *local_log_collector[NR_LOG_SIZES] = {
    NULL,
};

static flist_t *global_idx_list = NULL;
static __thread flist_t *local_idx_provider = NULL;
static __thread flist_t *local_idx_collector = NULL;

static flist_t *global_mmio_list = NULL;
static __thread flist_t *local_mmio_provider = NULL;
static __thread flist_t *local_mmio_collector = NULL;

static flist_t *global_table_list = NULL;
static __thread flist_t *local_table_provider = NULL;
static __thread flist_t *local_table_collector = NULL;

flist_t *alloc_flist(unsigned long skip_unit) {
  flist_t *new_list;
  int s;

  new_list = (flist_t *)malloc(sizeof(flist_t));

  if (__glibc_unlikely(new_list == NULL)) {
    HANDLE_ERROR("malloc");
  }

  new_list->list_head.next = NULL;
  new_list->skip_head.next = NULL;
  new_list->list_cnt = 0;
  new_list->skip_cnt = 0;
  new_list->skip_unit = skip_unit;

  s = pthread_mutex_init(&new_list->mutex, NULL);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("pthread_mutex_init");
  }

  return new_list;
}

inline fnode_t *get_fnode(void) {
  fnode_t *fnode;
  fnode = (fnode_t *)malloc(sizeof(fnode));
  if (__glibc_unlikely(fnode == NULL)) {
    HANDLE_ERROR("malloc");
  }
  return fnode;
}

inline void put_fnode(fnode_t *fnode) { free(fnode); }

/*
 * Before calling put_global(), global->mutex lock must be acquired.
 */
void put_global(fnode_t *node, flist_t *global) {
  if (__glibc_unlikely(global->list_cnt % global->skip_unit == 0)) {
    slist_push(&node->skip, &global->skip_head);
    global->skip_cnt++;
  }
  slist_push(&node->list, &global->list_head);
  global->list_cnt++;
}

void fill_local_provider(flist_t *local_provider, flist_t *global) {
  fnode_t *node;

  MUTEX_LOCK(&global->mutex);

  if (__glibc_unlikely(slist_empty(&global->skip_head))) {
    HANDLE_ERROR("global->skip_list is empty");
  }

  node = SLIST_ENTRY(global->skip_head.next, fnode_t, skip);
  slist_cut_splice(&global->list_head, &node->list, &local_provider->list_head);
  slist_pop(&global->skip_head);

  MUTEX_UNLOCK(&global->mutex);
}

void put_local_collector(fnode_t *node, flist_t *collector, flist_t *global) {
  if (__glibc_unlikely(collector->list_cnt % collector->skip_unit == 0)) {
    if (collector->skip_cnt >= MAX_SKIP_NODES) {
      fnode_t *skip_node;
      unsigned long count;

      MUTEX_LOCK(&global->mutex);

      skip_node = SLIST_ENTRY(collector->skip_head.next, fnode_t, skip);
      slist_cut_splice(&collector->list_head, &skip_node->list,
                       &global->list_head);
      slist_pop(&collector->skip_head);
      slist_push(&skip_node->skip, &global->skip_head);

      count = collector->skip_unit;
      collector->list_cnt -= count;
      global->list_cnt += count;
      collector->skip_cnt--;
      global->skip_cnt++;

      MUTEX_UNLOCK(&global->mutex);
    }
    slist_push(&node->skip, &collector->skip_head);
    collector->skip_cnt++;
  }
  slist_push(&node->list, &collector->list_head);
  collector->list_cnt++;
}

static int get_env(void) {
  char *pmem_path;
  size_t len;

  pmem_path = getenv("PMEM_PATH");
  if (pmem_path == NULL) {
    len = strlen(DEFAULT_PMEM_PATH) + 1;
    pmem_path = (char *)malloc(len);
    if (__glibc_unlikely(pmem_path == NULL)) {
      HANDLE_ERROR("malloc");
    }
    strcpy(pmem_path, DEFAULT_PMEM_PATH);
  }

  len = strlen(pmem_path);

  if (pmem_path[len - 1] == '/') {
    pmem_path[len - 1] = '\0';
  }

  libnvmmio_pid = getpid();

  sprintf(logdir_path, DIR_PATH, pmem_path, libnvmmio_pid);
  PRINT("logdir_path=%s", logdir_path);
  return mkdir(logdir_path, 0700);
}

static void __attribute__((destructor)) remove_logs(void) {
  char filename[1024];
  DIR *dirptr = NULL;
  struct dirent *file = NULL;
  int s;

  dirptr = opendir(logdir_path);
  if (__glibc_unlikely(dirptr == NULL)) {
    HANDLE_ERROR("opendir");
  }

  while ((file = readdir(dirptr)) != NULL) {
    if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
      continue;
    }
    sprintf(filename, "%s/%s", logdir_path, file->d_name);

    s = unlink(filename);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("unlink");
    }
  }
  s = closedir(dirptr);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("unlink");
  }

  s = rmdir(logdir_path);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("rmdir");
  }

  PRINT("finished");
}

static void *mmap_logfile(const char *path, size_t len) {
  void *addr;
  int fd, flags, s;

  if (path == NULL) {
    fd = -1;
    flags = MAP_ANONYMOUS | MAP_SHARED;
  } else {
    fd = posix.open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (__glibc_unlikely(fd == -1)) {
      HANDLE_ERROR("open(%s)", path);
    }

    s = posix_fallocate(fd, 0, len);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("posix_fallocate, len=%lu", len);
    }
    flags = MAP_SHARED | MAP_POPULATE;
  }

  addr = mmap(0, len, PROT_READ | PROT_WRITE, flags, fd, 0);
  if (__glibc_unlikely(addr == MAP_FAILED)) {
    HANDLE_ERROR("mmap");
  }

  PRINT("file=%s, len=%s", path, get_readable_size(len));
  return addr;
}

static void *alloc_pmem(char *name, int number, size_t size) {
  char filename[1024];
  sprintf(filename, "%s/%s-%d.log", logdir_path, name, number);
  return mmap_logfile(filename, size);
}

static void create_global_list(void *addr, size_t size, unsigned long count,
                               flist_t *global, void (*init_obj)(void *obj)) {
  void *obj;
  unsigned long i;

  MUTEX_LOCK(&global->mutex);

  for (i = 0; i < count; i++) {
    obj = addr + (i * size);
    if (init_obj) {
      init_obj(obj);
    }
    PUSH_GLOBAL(obj, global);
  }

  MUTEX_UNLOCK(&global->mutex);
}

static void init_table(void *obj) { memset(obj, 0, sizeof(log_table_t)); }

static void create_global_table_list(void) {
  void *addr;
  size_t mem_size, table_size;
  unsigned long count;

  count = NR_ALLOC_TABLES - (NR_ALLOC_TABLES % NR_NODE_FILL);
  table_size = sizeof(log_table_t);
  mem_size = table_size * count;

  addr = (void *)malloc(mem_size);
  if (__glibc_unlikely(addr == NULL)) {
    HANDLE_ERROR("malloc");
  }
  PRINT("pre-allocated memory: %s", get_readable_size(mem_size));

  global_table_list = alloc_flist(NR_NODE_FILL);
  create_global_list(addr, table_size, count, global_table_list, init_table);

  PRINT("the number of nodes: %lu, log_table_t size: %lu", count, table_size);
}

static void create_global_log_list(void) {
  void *addr;
  size_t log_size, file_size;
  int count;
  int i;

  for (i = 0; i < NR_LOG_SIZES; i++) {
    log_size = 1UL << LOG_SHIFT(i);
    count = LOG_FILE_SIZE >> LOG_SHIFT(i);
    count = count - (count % NR_NODE_FILL);
    file_size = log_size * count;
    addr = alloc_pmem("logs", i, file_size);

    global_log_list[i] = alloc_flist(NR_NODE_FILL);
    create_global_list(addr, log_size, count, global_log_list[i], NULL);

    PRINT("the number of nodes: %d, log size: %lu", count, log_size);
  }
}

static void init_idx(void *obj) {
  idx_entry_t *entry;
  int s;

  entry = (idx_entry_t *)obj;

  entry->rwlockp = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
  if (__glibc_unlikely(entry->rwlockp == NULL)) {
    HANDLE_ERROR("malloc");
  }

  s = pthread_rwlock_init(entry->rwlockp, NULL);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("pthread_rwlock_init");
  }
  entry->united = 0;
  entry->dst = NULL;
  entry->log = NULL;
}

static void create_global_idx_list(void) {
  void *addr;
  size_t size;
  unsigned long count;
  int i;

  count = LOG_FILE_SIZE >> PAGE_SHIFT;
  for (i = 1; i < NR_LOG_SIZES; i++) {
    count += count / 2;
  }

  size = count * sizeof(idx_entry_t);
  addr = alloc_pmem("index", 0, size);

  global_idx_list = alloc_flist(NR_NODE_FILL);
  create_global_list(addr, sizeof(idx_entry_t), count, global_idx_list,
                     init_idx);

  PRINT("the number of nodes: %lu, idx_entry_t size: %lu", count,
        sizeof(idx_entry_t));
}

static void init_mmio(void *obj) {
  mmio_t *mmio;
  int s;

  mmio = (mmio_t *)obj;

  s = pthread_rwlock_init(&mmio->rwlock, NULL);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("pthread_rwlock_init");
  }

  mmio->start = NULL;
  mmio->end = NULL;
  mmio->ino = 0;
  mmio->offset = 0;
  mmio->policy = DEFAULT_POLICY;
  mmio->radixlog.lgd = NULL;
  mmio->radixlog.skip = NULL;
  mmio->read = 0;
  mmio->write = 0;
  mmio->fsize = 0;
}

static void create_global_mmio_list(void) {
  void *addr;
  size_t mem_size, mmio_size;
  unsigned long count;

  mmio_size = sizeof(mmio_t);
  count = NR_MMIOS - (NR_MMIOS % NR_MMIO_FILL);
  mem_size = mmio_size * count;
  addr = alloc_pmem("mmio", 0, mem_size);

  global_mmio_list = alloc_flist(NR_MMIO_FILL);
  create_global_list(addr, mmio_size, count, global_mmio_list, init_mmio);

  PRINT("the number of nodes: %lu, log size: %lu", count, mmio_size);
}

static inline int get_prot(int flags) {
  int prot;
  /*
   * O_RDONLY  0x0000 : open for reading only
   * O_WRONLY  0x0001 : open for writing only
   * O_RDWR    0x0002 : open for reading and writing
   * O_ACCMODE 0x0003 : mask for above modes
   */
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      prot = PROT_READ;
      break;
    case O_WRONLY:
      prot = PROT_WRITE;
      break;
    case O_RDWR:
      prot = PROT_READ | PROT_WRITE;
      break;
    default:
      HANDLE_ERROR("flags is wrong.");
  }
  return prot;
}

mmio_t *get_new_mmio(int fd, int flags, unsigned long ino,
                     unsigned long fsize) {
  mmio_t *mmio = NULL;
  void *addr;
  unsigned long len;
  int s, prot;

  POP_PROVIDER(mmio, mmio_t, local_mmio_provider, global_mmio_list);

  if (fsize == 0) {
    len = DEFAULT_MMAP_SIZE;
    s = posix_fallocate(fd, 0, len);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("posix_fallocate");
    }
  } else {
    len = fsize;
  }

  prot = get_prot(flags);

  addr = mmap(NULL, len, prot, MAP_SHARED | MAP_POPULATE, fd, 0);
  if (__glibc_unlikely(addr == MAP_FAILED)) {
    HANDLE_ERROR("mmap");
  }

  init_radixlog(&mmio->radixlog, len);
  mmio->start = addr;
  mmio->end = addr + len;
  mmio->fsize = fsize;
  mmio->ino = ino;
  create_checkpoint_thread(mmio);

  return mmio;
}

void release_mmio(mmio_t *mmio, int flags, int fd) {
  int s;

  if (mmio->checkpoint_thread) {
    s = pthread_cancel(mmio->checkpoint_thread);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("pthread_cancel");
    }
    PRINT("canceled checkpoint thread.");
  }

  s = munmap(mmio->start, mmio->end - mmio->start);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("munmap");
  }
  PRINT("unmapped memory-mapped file");

  if ((flags & O_ACCMODE) > 0) {
    s = posix.ftruncate(fd, mmio->fsize);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("ftruncate");
    }
    PRINT("resized the file size: %lu -> %lu", mmio->end - mmio->start,
          mmio->fsize);
  }

  s = pthread_rwlock_destroy(&mmio->rwlock);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("pthread_rwlock_destroy");
  }

  init_mmio(mmio);
  PUSH_COLLECTOR(mmio, local_mmio_collector, global_mmio_list);
}

log_table_t *alloc_log_table(table_type_t type) {
  log_table_t *table;

  PRINT("table type: %lu", (unsigned long)type);
  POP_PROVIDER(table, log_table_t, local_table_provider, global_table_list);

  table->type = type;
  table->log_size = NR_LOG_SIZES;

  return table;
}

void free_log_table(log_table_t *table) {
  PUSH_COLLECTOR(table, local_table_collector, global_table_list);
}

void *alloc_log_data(log_size_t log_size) {
  void *data;
  POP_PROVIDER(data, void *, local_log_provider[log_size],
               global_log_list[log_size]);
  return data;
}

void free_log_data(void *data, log_size_t log_size) {
  PUSH_COLLECTOR(data, local_log_collector[log_size],
                 global_log_list[log_size]);
}

idx_entry_t *alloc_idx_entry(log_size_t log_size) {
  idx_entry_t *entry;

  POP_PROVIDER(entry, idx_entry_t, local_idx_provider, global_idx_list);

  entry->log = alloc_log_data(log_size);
  entry->list.next = NULL;
  RWLOCK_INIT(entry->rwlockp);

  return entry;
}

void free_idx_entry(idx_entry_t *entry, log_size_t log_size) {
  free_log_data(entry->log, log_size);
  entry->log = NULL;
  entry->united = 0;
  entry->dst = NULL;
  RWLOCK_DESTROY(entry->rwlockp);
  PUSH_COLLECTOR(entry, local_idx_collector, global_idx_list);
}

void init_allocator(void) {
  get_env();

  create_global_idx_list();
  create_global_log_list();
  create_global_mmio_list();
  create_global_table_list();
}
