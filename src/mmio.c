#define _GNU_SOURCE
#include "mmio.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "allocator.h"
#include "config.h"
#include "debug.h"
#include "lock.h"

#define LOCK_ENTRIES(type_, mmio_, offset_, len_, entries_head_)         \
  do {                                                                   \
    log_table_t *table_;                                                 \
    idx_entry_t *entry_;                                                 \
    log_size_t log_size_;                                                \
    unsigned long index_, log_offset_, log_len_;                         \
    int n_ = len_;                                                       \
    unsigned long off_ = offset_;                                        \
                                                                         \
    while (n_ > 0) {                                                     \
      table_ = get_log_table(&mmio_->radixlog, off_);                    \
      log_size_ = get_log_size(table_, off_, n_);                        \
      PRINT("log_size=%lu", LOG_SIZE(log_size_));                        \
      index_ = TABLE_INDEX(log_size_, off_);                             \
      PRINT("TABLE index=%lu", index_);                                  \
                                                                         \
      do {                                                               \
        entry_ = get_log_entry(mmio_->epoch, table_, index_, log_size_); \
      } while (pthread_rwlock_try##type_##lock(entry_->rwlockp) != 0);   \
                                                                         \
      PRINT(#type_ "lock idx_entry, offset=%lu", offset_);               \
                                                                         \
      entry_->log_size = log_size_;                                      \
      slist_push(&entry_->list, &entries_head_);                         \
                                                                         \
      log_offset_ = off_ & (LOG_SIZE(log_size_) - 1);                    \
      log_len_ = LOG_SIZE(log_size_) - log_offset_;                      \
      n_ -= log_len_;                                                    \
      off_ += log_len_;                                                  \
    }                                                                    \
  } while (0)

#define UNLOCK_ENTRIES(entries_head_)                    \
  do {                                                   \
    idx_entry_t *entry_;                                 \
    SLIST_FOR_EACH_ENTRY(entry_, &entries_head_, list) { \
      RWLOCK_UNLOCK(entry_->rwlockp);                    \
      PRINT("unlock idx_entry");                         \
    }                                                    \
  } while (0)

static inline bool check_expend(mmio_t *mmio, off_t offset, size_t len) {
  if (mmio->end < (mmio->start + offset + len)) {
    return true;
  } else {
    return false;
  }
}

static inline bool check_fsize(mmio_t *mmio, off_t offset, off_t len) {
  if (mmio->fsize < (offset + len)) {
    return true;
  } else {
    return false;
  }
}

static inline void increase_counter(unsigned long *cnt) {
  unsigned long old, new;

  do {
    old = *cnt;
    new = *cnt + 1;
  } while (!__sync_bool_compare_and_swap(cnt, old, new));
}

/*
 * TODO: check conditions
 * 1. FS space
 * 2. RLIMIT_FSIZE
 * 3. interrupt
 */
static void expend_mmio(mmio_t *mmio, int fd, off_t offset, size_t len) {
  unsigned long current_len, new_len;
  int s;

  RWLOCK_UNLOCK(&mmio->rwlock);
  RWLOCK_WRITE_LOCK(&mmio->rwlock);

  while (check_expend(mmio, offset, len)) {
    current_len = mmio->end - mmio->start;
    if (current_len >= BASIC_MMAP_SIZE) {
      new_len = current_len << 1;
    } else {
      new_len = BASIC_MMAP_SIZE;
    }

    s = posix_fallocate(fd, 0, new_len);
    if (__glibc_unlikely(s != 0)) {
      HANDLE_ERROR("fallocate");
    }

    mmio->start = mremap(mmio->start, current_len, new_len, MREMAP_MAYMOVE);
    if (__glibc_unlikely(mmio->start == MAP_FAILED)) {
      HANDLE_ERROR("mremap");
    }
    mmio->end = mmio->start + new_len;
  }
  PRINT("expend memory-mapped file: %lu", current_len);

  RWLOCK_UNLOCK(&mmio->rwlock);
  RWLOCK_READ_LOCK(&mmio->rwlock);
}

//                (1)                  (2)                  (3)
//              _______              -------              -------
//              |     |              |     |              |     |
//  REQ_START-->|/////|  REQ_START-->|/////|  REQ_START-->|/////|
//              |/////|              |/////|              |/////|
//              |/////|  log_start-->|XXXXX|  log_start-->|XXXXX|
//    REQ_END-->|     |              |XXXXX|              |XXXXX|
//              |     |              |XXXXX|              |XXXXX|
//  log_start-->|\\\\\|              |XXXXX|              |XXXXX|
//              |\\\\\|    REQ_END-->|\\\\\|    log_end-->|/////|
//              |\\\\\|              |\\\\\|              |/////|
//    log_end-->|     |    log_end-->|     |    REQ_END-->|     |
//              -------              -------              -------
//                Log                  Log                  Log
//
//                (4)                  (5)                  (6)
//              _______              -------              -------
//              |     |              |     |              |     |
//  log_start-->|\\\\\|  log_start-->|\\\\\|  log_start-->|\\\\\|
//              |\\\\\|              |\\\\\|              |\\\\\|
//  REQ_START-->|XXXXX|  REQ_START-->|XXXXX|              |\\\\\|
//              |XXXXX|              |XXXXX|    log_end-->|     |
//              |XXXXX|              |XXXXX|              |     |
//              |XXXXX|              |XXXXX|  REQ_START-->|/////|
//    REQ_END-->|\\\\\|    log_end-->|/////|              |/////|
//              |\\\\\|              |/////|              |/////|
//    log_end-->|     |    REQ_END-->|     |    REQ_END-->|     |
//              -------              -------              -------
//                Log                  Log                  Log
//
static inline int check_log(void *req_start, void *req_end, void *log_start,
                            void *log_end) {
  if (req_start <= log_start) {
    if (req_end >= log_start) {
      if (req_end < log_end)
        return 2;
      else
        return 3;
    } else
      return 1;
  } else {
    if (req_end <= log_end)
      return 4;
    else {
      if (log_end < req_start)
        return 6;
      else
        return 5;
    }
  }
}

void checkpoint_mmio(mmio_t *mmio) {
  log_table_t *table;
  idx_entry_t *entry;
  log_size_t log_size;
  void *dst, *src;
  unsigned long i, current_epoch, offset, endoff;

  PRINT("start checkpointing: mmio->ino=%lu", mmio->ino);

  offset = 0;
  endoff = mmio->end - mmio->start;
  current_epoch = mmio->epoch;

  while (offset < endoff) {
    log_size = LOG_4K;
    RWLOCK_READ_LOCK(&mmio->rwlock);
    if (pthread_rwlock_tryrdlock(&mmio->rwlock) == 0) {
      table = find_log_table(&mmio->radixlog, offset);
      if (table) {
        log_size = table->log_size;
        for (i = 0; i < NR_ENTRIES(log_size); i++) {
          entry = table->entries[i];

          if (entry && entry->epoch < current_epoch) {
            if (RWLOCK_WRITE_TRYLOCK(entry->rwlockp)) {
              if (entry->epoch < current_epoch) {
                if (entry->policy == REDO) {
                  dst = entry->dst + entry->offset;
                  src = entry->log + entry->offset;
                  NTSTORE(dst, src, entry->len);
                  PRINT("ntstore(%p, %p, %u)", dst, src, entry->len);
                  FENCE();
                  PRINT("mfence()");
                }
                table->entries[i] = NULL;
                free_idx_entry(entry, log_size);
                PRINT("clear the idx_entry: offset=%lu, table idx=%lu", offset,
                      i);
                continue;
              }
              RWLOCK_UNLOCK(entry->rwlockp);
            }
          }
        }
      }
      RWLOCK_UNLOCK(&mmio->rwlock);
    }
    offset += NR_ENTRIES(log_size) * LOG_SIZE(log_size);
  }
  PRINT("complete checkpointing mmio->ino=%lu", mmio->ino);
}

static void *checkpoint_thread_func(void *parm) {
  mmio_t *mmio;
  mmio = (mmio_t *)parm;

  while (true) {
    usleep((useconds_t)SYNC_PERIOD);
    PRINT("wake up");
    checkpoint_mmio(mmio);
  }

  return NULL;
}

void create_checkpoint_thread(mmio_t *mmio) {
  int s;

  s = pthread_create(&mmio->checkpoint_thread, NULL, checkpoint_thread_func,
                     mmio);
  if (__glibc_unlikely(s != 0)) {
    HANDLE_ERROR("pthread_create");
  }
  PRINT("create checkpoint thread: %lu",
        (unsigned long)mmio->checkpoint_thread);
}

inline static void checkpoint_entry(mmio_t *mmio, idx_entry_t *entry) {
  void *dst, *src;

  if (entry->policy == REDO) {
    dst = entry->dst + entry->offset;
    src = entry->log + entry->offset;
    NTSTORE(dst, src, entry->len);
    FENCE();
  }
  entry->epoch = mmio->epoch;
  entry->policy = mmio->policy;
  entry->len = 0;
  entry->offset = 0;
  FLUSH(entry, sizeof(idx_entry_t));
  FENCE();
}

ssize_t mmio_write(mmio_t *mmio, int fd, off_t offset, const void *buf,
                   off_t len) {
  idx_entry_t *entry;
  unsigned long log_offset, log_len;
  void *log_start, *dst, *src;
  off_t off;
  off_t ret = 0;
  log_size_t log_size;
  int n;
  SLIST_HEAD(entries_head);

  PRINT("mmio=%p, offset=%ld, buf=%p, len=%ld", mmio, offset, buf, len);

  /*
   * Acquire the reader-locks of the mmio.
   */
  RWLOCK_READ_LOCK(&mmio->rwlock);

  if (__glibc_unlikely(check_expend(mmio, offset, len))) {
    expend_mmio(mmio, fd, offset, len);
  }

  /*
   * Acquire all writer-locks of required logs.
   */
  LOCK_ENTRIES(wr, mmio, offset, len, entries_head);

  /*
   * Perform the write
   */
  n = len;
  off = offset;
  dst = mmio->start + offset;
  src = (void *)buf;

  SLIST_FOR_EACH_ENTRY(entry, &entries_head, list) {
    if (entry->epoch < mmio->epoch) {
      checkpoint_entry(mmio, entry);
    }
    log_size = entry->log_size;
    log_offset = off & (LOG_SIZE(log_size) - 1);
    log_start = entry->log + log_offset;
    log_len = LOG_SIZE(log_size) - log_offset;

    if ((unsigned long)n > log_len) {
      n = log_len;
    }

    switch (mmio->policy) {
      case UNDO:
        /* log <= original data */
        NTSTORE(log_start, dst, n);
        PRINT("undo logging: ntstore(%p, %p, %d)", log_start, dst, n);
        break;
      case REDO:
        /* log <= new data */
        NTSTORE(log_start, src, n);
        PRINT("redo logging: ntstore(%p, %p, %d)", log_start, src, n);
        break;
      default:
        HANDLE_ERROR("policy error");
        break;
    }

    /* If data already exists in the log (overwriting) */
    if (entry->len > 0 && (unsigned long)n != LOG_SIZE(log_size)) {
      void *log_end, *prev_start, *prev_end, *overwrite_src;
      size_t overwrite_len;

      log_end = log_start + n;
      prev_start = entry->log + entry->offset;
      prev_end = prev_start + entry->len;

      switch (check_log(log_start, log_end, prev_start, prev_end)) {
        case 1:
          PRINT("overwrite case 1");
          overwrite_src = dst + n;
          overwrite_len = prev_start - log_end;
          NTSTORE(log_end, overwrite_src, overwrite_len);
          entry->offset = log_offset;
          entry->len = prev_end - log_start;
          break;
        case 2:
          PRINT("overwrite case 2");
          entry->offset = log_offset;
          entry->len = prev_end - log_start;
          break;
        case 3:
          PRINT("overwrite case 3");
          entry->offset = log_offset;
          entry->len = n;
          break;
        case 4:
          PRINT("overwrite case 4");
          break;
        case 5:
          PRINT("overwrite case 5");
          entry->len = log_end - prev_start;
          break;
        case 6:
          PRINT("overwrite case 6");
          overwrite_len = log_start - prev_end;
          overwrite_src = dst - overwrite_len;
          NTSTORE(prev_end, overwrite_src, overwrite_len);
          entry->len = log_end - prev_start;
          break;
        default:
          HANDLE_ERROR("check overwrite");
          break;
      }
    } else {
      entry->offset = log_offset;
      entry->len = n;
      entry->dst = (void *)((unsigned long)dst & LOG_MASK(log_size));
      entry->policy = mmio->policy;
    }
    FLUSH(entry, sizeof(idx_entry_t));
    PRINT("cache flush after updating the idx_entry");

    ret += n;
    off += n;
    dst += n;
    src += n;
  }
  increase_counter(&mmio->write);
  FENCE();
  PRINT("mfence");

  if (mmio->policy == UNDO) {
    NTSTORE(mmio->start + offset, buf, len);
    PRINT("update the file after undo logging: ntstore(%p, %p, %lu)",
          mmio->start + offset, buf, len);
    FENCE();
    PRINT("mfence");
  }

  if (mmio->fsize < offset + ret) {
    mmio->fsize = offset + ret;
    PRINT("update mmio->fsize=%lu", mmio->fsize);
  }

  /*
   * Release all writer-locks.
   */
  UNLOCK_ENTRIES(entries_head);

  /*
   * Release the reader-lock of the mmio.
   */
  RWLOCK_UNLOCK(&mmio->rwlock);

  return (ssize_t)ret;
}

inline ssize_t read_redolog(struct slist_head *entries_head, void *dst,
                            void *file_addr, unsigned long offset,
                            unsigned long len) {
  idx_entry_t *entry;
  void *log_start, *log_end, *req_start, *req_end, *src;
  unsigned long log_offset, n, log_max_len;
  log_size_t log_size;

  SLIST_FOR_EACH_ENTRY(entry, entries_head, list) {
    n = len;
    log_size = entry->log_size;
    log_offset = offset & (LOG_SIZE(log_size) - 1);
    log_max_len = LOG_SIZE(log_size) - log_offset;

    if (n > log_max_len) {
      n = log_max_len;
    }

    if (entry->len > 0) {
      /* If the redo log exists */
      log_start = entry->log + entry->offset;
      log_end = log_start + entry->len;
      req_start = entry->log + log_offset;
      req_end = req_start + n;

      switch (check_log(req_start, req_end, log_start, log_end)) {
        case 1:
          src = file_addr + offset;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        case 2:
          src = file_addr + offset;
          n = log_start - req_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          offset += n;
          len -= n;
          dst += n;
          src = log_start;
          n = req_end - log_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        case 3:
          src = file_addr + offset;
          n = log_start - req_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          offset += n;
          len -= n;
          dst += n;
          src = log_start;
          n = log_end - log_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          offset += n;
          len -= n;
          dst += n;
          src = (file_addr + offset) + (log_end - req_start);
          n = req_end - log_end;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        case 4:
          src = req_start;
          n = req_end - req_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        case 5:
          src = req_start;
          n = log_end - req_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          offset += n;
          len -= n;
          dst += n;
          src = (file_addr + offset);
          n = req_end - log_end;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        case 6:
          src = file_addr + offset;
          n = req_end - req_start;
          memcpy(dst, src, n);
          PRINT("read from redo: memcpy(%p, %p, %lu)", dst, src, n);
          dst += n;
          offset += n;
          len -= n;
          break;
        default:
          HANDLE_ERROR("check log");
          break;
      }
    } else {
      /* If the log does not exist */
      memcpy(dst, file_addr + offset, n);
      PRINT("no redo log: memcpy(%p, %p, %lu)", dst, file_addr + offset, n);
      dst += n;
      offset += n;
      len -= n;
    }
  }
  return 0;
}

ssize_t mmio_read(mmio_t *mmio, off_t offset, void *buf, size_t len) {
  SLIST_HEAD(entries_head);

  /*
   * Acquire the reader-locks of the mmio.
   */
  RWLOCK_READ_LOCK(&mmio->rwlock);

  /*
   * Acquire all reader-locks of required logs.
   */
  LOCK_ENTRIES(rd, mmio, offset, len, entries_head);

  /*
   * Check the file size to see if the requested read is possible.
   */
  if (check_fsize(mmio, offset, len)) {
    len = mmio->fsize - offset;
    PRINT("the requested length exceeds the file size. the reset length=%lu",
          len);
  }

  /*
   * Perform the read
   */
  switch (mmio->policy) {
    case UNDO:
      /* original file => buf */
      memcpy(buf, mmio->start + offset, len);
      PRINT("read from the file: memcpy(%p, %p, %lu)", buf,
            mmio->start + offset, len);
      break;
    case REDO:
      /* logs & original file => buf */
      read_redolog(&entries_head, buf, mmio->start, offset, len);
      break;
    default:
      HANDLE_ERROR("policy error");
      break;
  }
  increase_counter(&mmio->read);

  /*
   * Release all reader-locks.
   */
  UNLOCK_ENTRIES(entries_head);

  /*
   * Release the reader-lock of the mmio.
   */
  RWLOCK_UNLOCK(&mmio->rwlock);

  return len;
}

static inline void determine_policy(mmio_t *mmio) {
  unsigned long total_cnt, write_ratio;
  policy_t new_policy;

  total_cnt = mmio->read + mmio->write;

  if (total_cnt > 0) {
    write_ratio = mmio->write / total_cnt * 100;

    if (write_ratio > HYBRID_WRITE_RATIO) {
      new_policy = REDO;
    } else {
      new_policy = UNDO;
    }

    mmio->read = 0;
    mmio->write = 0;

    if (mmio->policy != new_policy) {
      checkpoint_mmio(mmio);
    }
  }
}

void commit_mmio(mmio_t *mmio) {
  /*
   * Acquire the writer-lock of the mmio.
   */
  RWLOCK_WRITE_LOCK(&mmio->rwlock);

  /*
   * Increase the golbal epoch number
   */
  mmio->epoch++;
  FLUSH(&mmio->epoch, sizeof(unsigned long));
  FENCE();
  PRINT("epoch=%lu\n", mmio->epoch);

#if HYBRID_LOGGING
  /*
   * Determine the next logging policy
   */
  determine_policy(mmio);

#endif /* HYBRID_LOGGING */

  /*
   * Release the writer-lock of the mmio.
   */
  RWLOCK_UNLOCK(&mmio->rwlock);
}
