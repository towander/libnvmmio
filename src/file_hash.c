#include "file_hash.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "allocator.h"
#include "config.h"
#include "debug.h"
#include "list.h"
#include "lock.h"
#include "mmio.h"

typedef struct hash_head_struct {
  pthread_mutex_t mutex;
  struct list_head head;
} hash_head_t;

typedef struct hash_node_struct {
  void *ptr;
  struct list_head list;
} hash_node_t;

static hash_head_t file_hash[FILE_HASH_SIZE] = {
    0,
};

static inline int get_hash_index(unsigned long ino) {
  return ino % FILE_HASH_SIZE;
}

/*
 * Get the MMIO by inode number.
 * If there is no corresponding mmio, NULL is returned.
 */
mmio_t *get_mmio_hash(unsigned long ino) {
  mmio_t *mmio;
  hash_node_t *node;
  int index;
  index = get_hash_index(ino);

  /* Lock the list. */
  MUTEX_LOCK(&file_hash[index].mutex);

  if (!list_empty(&file_hash[index].head)) {
    LIST_FOR_EACH_ENTRY(node, &file_hash[index].head, list) {
      mmio = (mmio_t *)node->ptr;
      if (ino == mmio->ino) {
        mmio->ref++;
        /* Unlock the list. */
        MUTEX_UNLOCK(&file_hash[index].mutex);
        return mmio;
      }
    }
  }

  /* Unlock the list. */
  MUTEX_UNLOCK(&file_hash[index].mutex);
  return NULL;
}

mmio_t *put_mmio_hash(unsigned long ino, mmio_t *mmio) {
  mmio_t *tmp_mmio;
  hash_node_t *hnode;
  int index;

  index = get_hash_index(ino);

  /* Lock the list. */
  MUTEX_LOCK(&file_hash[index].mutex);

  /* First, check whether the same MMIO already exists in the list. */
  if (!list_empty(&file_hash[index].head)) {
    LIST_FOR_EACH_ENTRY(hnode, &file_hash[index].head, list) {
      tmp_mmio = (mmio_t *)hnode->ptr;
      if (ino == tmp_mmio->ino) {
        /* Unlock the list. */
        MUTEX_UNLOCK(&file_hash[index].mutex);
        return tmp_mmio;
      }
    }
  }

  /* If the MMIO is not in the list, add the MMIO to the list. */
  mmio->ref++;
  hnode = (hash_node_t *)malloc(sizeof(hash_node_t));
  if (__glibc_unlikely(hnode == NULL)) {
    HANDLE_ERROR("malloc");
  }
  hnode->ptr = mmio;
  list_add(&hnode->list, &file_hash[index].head);

  /* Unlock the list. */
  MUTEX_UNLOCK(&file_hash[index].mutex);
  return mmio;
}

void delete_mmio_hash(int fd, file_t *file) {
  hash_node_t *hnode, *tmp;
  mmio_t *mmio;
  unsigned long ino;
  int index;

  ino = file->ino;
  index = get_hash_index(ino);

  MUTEX_LOCK(&file_hash[index].mutex);

  if (!list_empty(&file_hash[index].head)) {
    LIST_FOR_EACH_ENTRY_SAFE(hnode, tmp, &file_hash[index].head, list) {
      mmio = (mmio_t *)hnode->ptr;
      if (ino == mmio->ino) {
        mmio->ref--;

        if (mmio->ref <= 0) {
          checkpoint_mmio(mmio);
          list_del(&hnode->list);
          free(hnode);
          release_mmio(mmio, file->flags, fd);
        }
      }
    }
  }

  MUTEX_UNLOCK(&file_hash[index].mutex);
}

void init_file_hash(void) {
  int i, s;

  for (i = 0; i < FILE_HASH_SIZE; i++) {
    INIT_LIST_HEAD(&file_hash[i].head);
    s = pthread_mutex_init(&file_hash[i].mutex, NULL);
    if (s != 0) {
      HANDLE_ERROR("pthread_mutex_init");
    }
  }
  PRINT("OK");
}
