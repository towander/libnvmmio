#include "radixlog.h"

#include <limits.h>
#include <stdbool.h>

#include "allocator.h"
#include "config.h"
#include "debug.h"
#include "slist.h"

#define ALLOC_TABLE(table, type, parent, index)                                \
  do {                                                                         \
    table = alloc_log_table(type);                                             \
    if (!__sync_bool_compare_and_swap(&parent->entries[index], NULL, table)) { \
      free_log_table(table);                                                   \
      table = parent->entries[index];                                          \
    }                                                                          \
  } while (0);

inline bool check_prev_table(unsigned long prev_table_index,
                             unsigned long offset) {
  if (prev_table_index == ((offset >> LMD_SHIFT) & TABLE_MASK)) {
    return true;
  }
  return false;
}

static inline void atomic_increase(int *count) {
  int old, new;

  do {
    old = *count;
    new = old + 1;
  } while (!__sync_bool_compare_and_swap(count, old, new));
}

inline table_type_t get_deepest_table_type(unsigned long maxoff) {
  if (maxoff >> LMD_SHIFT) {
    PRINT("%lu >> LMD_SHIFT = %lu", maxoff, maxoff >> LMD_SHIFT);
    if (maxoff >> LUD_SHIFT) {
      PRINT("%lu >> LUD_SHIFT = %lu", maxoff, maxoff >> LUD_SHIFT);
      if (maxoff >> LGD_SHIFT) {
        PRINT("%lu >> LGD_SHIFT = %lu", maxoff, maxoff >> LGD_SHIFT);
        PRINT("LGD");
        return LGD;
      }
      PRINT("LUD");
      return LUD;
    }
    PRINT("LMD");
    return LMD;
  }
  PRINT("TABLE");
  return TABLE;
}

void init_radixlog(radix_root_t *root, unsigned long filesize) {
  table_type_t deepest_table_type, type;
  log_table_t *parent, *table;
  unsigned long index, maxoff;

  deepest_table_type = get_deepest_table_type(filesize - 1);
  maxoff = filesize - 1;
  type = LGD;
  parent = NULL;

  do {
    table = alloc_log_table(type);

    if (parent != NULL) {
      switch (type) {
        case LUD:
          index = LGD_INDEX(maxoff);
          break;
        case LMD:
          index = LUD_INDEX(maxoff);
          break;
        case TABLE:
          index = LMD_INDEX(maxoff);
          break;
        default:
          HANDLE_ERROR("wrong table type");
      }
      parent->entries[index] = table;
    } else {
      root->lgd = table;
    }
    parent = table;
    type = NEXT_TABLE_TYPE(type);
  } while (type >= deepest_table_type);

  root->skip = table;
  root->prev_table_index = ULONG_MAX;
}

log_table_t *find_log_table(radix_root_t *root, unsigned long offset) {
  log_table_t *lgd, *lud, *lmd, *table = NULL;
  unsigned long index;

  if (root->skip) {
    switch (root->skip->type) {
      case TABLE:
        PRINT("skip to TABLE");
        table = root->skip;
        break;

      case LMD:
        PRINT("skip to LMD");
        lmd = root->skip;

        index = LMD_INDEX(offset);
        PRINT("LMD index=%lu", index);
        table = lmd->entries[index];

        if (table == NULL) {
          return NULL;
        }
        break;

      case LUD:
        PRINT("skip to LUD");
        lud = root->skip;

        index = LUD_INDEX(offset);
        PRINT("LUD index=%lu", index);
        lmd = lud->entries[index];

        if (lmd == NULL) {
          return NULL;
        }

        index = LMD_INDEX(offset);
        PRINT("LMD index=%lu", index);
        table = lmd->entries[index];

        if (table == NULL) {
          return NULL;
        }
        break;

      case LGD:
        PRINT("skip to LGD");
        lgd = root->skip;

        index = LGD_INDEX(offset);
        PRINT("LGD index=%lu", index);
        lud = lgd->entries[index];

        if (lud == NULL) {
          return NULL;
        }

        index = LUD_INDEX(offset);
        PRINT("LUD index=%lu", index);
        lmd = lud->entries[index];

        if (lmd == NULL) {
          return NULL;
        }

        index = LMD_INDEX(offset);
        PRINT("LMD index=%lu", index);
        table = lmd->entries[index];

        if (table == NULL) {
          return NULL;
        }
        break;

      default:
        HANDLE_ERROR("wrong table type");
        break;
    }
  }
  return table;
}

log_table_t *get_log_table(radix_root_t *root, unsigned long offset) {
  log_table_t *lgd, *lud, *lmd, *table;
  unsigned long index;

  if (check_prev_table(root->prev_table_index, offset)) {
    PRINT("reuse the previous table: prev=%lx, current=%lx",
          root->prev_table_index, offset);
    return root->prev_table;
  }

  switch (root->skip->type) {
    case TABLE:
      PRINT("skip to TABLE");
      table = root->skip;
      break;

    case LMD:
      PRINT("skip to LMD");
      lmd = root->skip;

      index = LMD_INDEX(offset);
      PRINT("LMD index=%lu", index);
      table = lmd->entries[index];

      if (table == NULL) {
        ALLOC_TABLE(table, TABLE, lmd, index);
      }
      break;

    case LUD:
      PRINT("skip to LUD");
      lud = root->skip;

      index = LUD_INDEX(offset);
      PRINT("LUD index=%lu", index);
      lmd = lud->entries[index];

      if (lmd == NULL) {
        ALLOC_TABLE(lmd, LMD, lud, index);
      }

      index = LMD_INDEX(offset);
      PRINT("LMD index=%lu", index);
      table = lmd->entries[index];

      if (table == NULL) {
        ALLOC_TABLE(table, TABLE, lmd, index);
      }
      break;

    case LGD:
      PRINT("skip to LGD");
      lgd = root->skip;

      index = LGD_INDEX(offset);
      PRINT("LGD index=%lu", index);
      lud = lgd->entries[index];

      if (lud == NULL) {
        ALLOC_TABLE(lud, LUD, lgd, index);
      }

      index = LUD_INDEX(offset);
      PRINT("LUD index=%lu", index);
      lmd = lud->entries[index];

      if (lmd == NULL) {
        ALLOC_TABLE(lmd, LMD, lud, index);
      }

      index = LMD_INDEX(offset);
      PRINT("LMD index=%lu", index);
      table = lmd->entries[index];

      if (table == NULL) {
        ALLOC_TABLE(table, TABLE, lmd, index);
      }
      break;

    default:
      HANDLE_ERROR("wrong table type");
      break;
  }

  root->prev_table = table;
  root->prev_table_index = TABLE_MASK & (offset >> LMD_SHIFT);
  return table;
}

inline log_size_t set_log_size(unsigned long offset, size_t len) {
  log_size_t log_size = LOG_4K;
  log_size_t max_log_size = NR_LOG_SIZES - 1;

  len += offset & (LOG_SIZE(log_size) - 1);
  len = (len - 1) >> LOG_SHIFT(log_size);

  while (len && log_size < max_log_size) {
    len = len >> 1;
    log_size++;
  }
  return log_size;
}

inline log_size_t get_log_size(log_table_t *table, unsigned long offset,
                               size_t len) {
  log_size_t log_size;

retry_get_log_size:
  if (table->log_size != NR_LOG_SIZES) {
    return table->log_size;
  } else {
    log_size = set_log_size(offset, len);

    if (!__sync_bool_compare_and_swap(&table->log_size, NR_LOG_SIZES,
                                      log_size)) {
      goto retry_get_log_size;
    }
    return log_size;
  }
}

inline idx_entry_t *get_log_entry(unsigned long epoch, log_table_t *table,
                                  unsigned long index, log_size_t log_size) {
  idx_entry_t *entry;

  entry = table->entries[index];

  if (entry == NULL) {
    entry = alloc_idx_entry(log_size);
    entry->epoch = epoch;

    if (!__sync_bool_compare_and_swap(&table->entries[index], NULL, entry)) {
      free_idx_entry(entry, log_size);
      entry = table->entries[index];
    }
  }

  return entry;
}
