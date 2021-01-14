#ifndef LIBNVMMIO_LOG_H
#define LIBNVMMIO_LOG_H

#include <pthread.h>
#include <stdbool.h>

#include "config.h"
#include "slist.h"

#define PTRS_PER_TABLE (1UL << 9)

#define PAGE_SHIFT (12)
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define LGD_SHIFT (39)
#define LUD_SHIFT (30)
#define LMD_SHIFT (21)
#define LOG_SHIFT(s) (LMD_SHIFT - ((LMD_SHIFT - PAGE_SHIFT) - s))
#define LOG_SIZE(s) (1UL << LOG_SHIFT(s))
#define LOG_MASK(s) (~(LOG_SIZE(s) - 1))
#define LOG_OFFSET(addr, s) (addr & (LOG_SIZE(s) - 1))
#define NR_ENTRIES(s) (1UL << (LMD_SHIFT - LOG_SHIFT(s)))

#define TABLE_MASK ((1UL << 27) - 1)

typedef enum table_type_enum { TABLE = 1, LMD, LUD, LGD } table_type_t;

typedef struct index_entry_struct {
  union {
    struct {
      unsigned long united;
    };
    struct {
      unsigned long epoch : 20;
      unsigned long offset : 21;
      unsigned long len : 22;
      unsigned long policy : 1;
    };
  };
  void *log;
  void *dst;
  pthread_rwlock_t *rwlockp;
  log_size_t log_size;
  struct slist_head list;
} idx_entry_t;

typedef struct table_struct {
  log_size_t log_size;
  table_type_t type;
  int index;
  void *entries[PTRS_PER_TABLE];
} log_table_t;

typedef struct radix_root_struct {
  log_table_t *lgd;
  log_table_t *skip;
  log_table_t *prev_table;
  unsigned long prev_table_index;
} radix_root_t;

#define LGD_INDEX(OFFSET) (OFFSET >> LGD_SHIFT) & (PTRS_PER_TABLE - 1)
#define LUD_INDEX(OFFSET) (OFFSET >> LUD_SHIFT) & (PTRS_PER_TABLE - 1)
#define LMD_INDEX(OFFSET) (OFFSET >> LMD_SHIFT) & (PTRS_PER_TABLE - 1)
#define TABLE_INDEX(LOGSIZE, OFFSET) \
  (OFFSET >> LOG_SHIFT(LOGSIZE)) & (NR_ENTRIES(LOGSIZE) - 1)

#define NEXT_TABLE_TYPE(TYPE) (TYPE - 1)

table_type_t get_deepest_table_type(unsigned long filesize);
void init_radixlog(radix_root_t *root, unsigned long filesize);
log_table_t *get_log_table(radix_root_t *root, unsigned long offset);
log_table_t *find_log_table(radix_root_t *root, unsigned long offset);
log_size_t set_log_size(unsigned long offset, size_t len);
log_size_t get_log_size(log_table_t *table, unsigned long offset, size_t len);
idx_entry_t *get_log_entry(unsigned long epoch, log_table_t *table,
                           unsigned long index, log_size_t log_size);
bool check_prev_table(unsigned long prev_table_index, unsigned long offset);

#endif /* LIBNVMMIO_LOG_H */
