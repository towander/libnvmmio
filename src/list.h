#ifndef LIBNVMMIO_LIST_H
#define LIBNVMMIO_LIST_H

#include <stdbool.h>

struct list_head {
  struct list_head *next;
  struct list_head *prev;
};

#define LIST_HEAD_INIT(name_) \
  { &(name_), &(name_) }

#define LIST_HEAD(name_) struct list_head name_ = LIST_HEAD_INIT(name_)

static inline void INIT_LIST_HEAD(struct list_head *list) {
  list->next = list->prev = list;
}

static inline void __list_add(struct list_head *entry, struct list_head *prev,
                              struct list_head *next) {
  next->prev = entry;
  entry->next = next;
  entry->prev = prev;
  prev->next = entry;
}

static inline void list_add(struct list_head *entry, struct list_head *head) {
  __list_add(entry, head, head->next);
}
static inline void list_add_tail(struct list_head *entry,
                                 struct list_head *head) {
  __list_add(entry, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next) {
  next->prev = prev;
  prev->next = next;
}

static inline void list_del(struct list_head *entry) {
  __list_del(entry->prev, entry->next);
}

static inline void list_del_init(struct list_head *entry) {
  __list_del(entry->prev, entry->next);
  INIT_LIST_HEAD(entry);
}

static inline void list_move_tail(struct list_head *list,
                                  struct list_head *head) {
  __list_del(list->prev, list->next);
  list_add_tail(list, head);
}

static inline bool list_empty(struct list_head *head) {
  return head->next == head;
}

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) \
  (type *)((char *)(ptr) - (char *)&((type *)0)->member)
#endif

#define __CONTAINER_OF(ptr, sample, member) \
  (void *)CONTAINER_OF((ptr), typeof(*(sample)), member)

#define LIST_FOR_EACH_ENTRY(pos, head, member)          \
  for (pos = __CONTAINER_OF((head)->next, pos, member); \
       &pos->member != (head);                          \
       pos = __CONTAINER_OF(pos->member.next, pos, member))

#define LIST_FOR_EACH_ENTRY_SAFE(pos, tmp, head, member)   \
  for (pos = __CONTAINER_OF((head)->next, pos, member),    \
      tmp = __CONTAINER_OF(pos->member.next, pos, member); \
       &pos->member != (head);                             \
       pos = tmp, tmp = __CONTAINER_OF(pos->member.next, tmp, member))

#endif /* LIBNVMMIO_LIST_H */
