#ifndef LIBNVMMIO_SLIST_H
#define LIBNVMMIO_SLIST_H

#include <stddef.h>
#include <stdio.h>

struct slist_head {
  struct slist_head *next;
};

#define SLIST_HEAD(name) struct slist_head name = {NULL};

#define SCONTAINER_OF(ptr_, type_, member_)               \
  ({                                                     \
    const typeof(((type_ *)0)->member_) *mptr_ = (ptr_); \
    (type_ *)((char *)mptr_ - offsetof(type_, member_)); \
  })

#define SLIST_ENTRY(ptr_, type_, member_) SCONTAINER_OF(ptr_, type_, member_)

#define SLIST_FIRST_ENTRY(ptr_, type_, member_) \
  SLIST_ENTRY((ptr_)->next, type_, member_)

#define SLIST_NEXT_ENTRY(pos_, member_) \
  SLIST_ENTRY((pos_)->member_.next, typeof(*(pos_)), member_)

#define SLIST_FOR_EACH_ENTRY(pos_, head_, member_)              \
  for (pos_ = SLIST_FIRST_ENTRY(head_, typeof(*pos_), member_); \
       &pos_->member_ != NULL; pos_ = SLIST_NEXT_ENTRY(pos_, member_))

static inline void slist_push(struct slist_head *new_node,
                              struct slist_head *head) {
  new_node->next = head->next;
  head->next = new_node;
}

static inline void __slist_del(struct slist_head *head,
                               struct slist_head *next) {
  head->next = next;
}

static inline struct slist_head *slist_pop(struct slist_head *head) {
  struct slist_head *entry;
  entry = head->next;
  __slist_del(head, entry->next);
  entry->next = NULL;
  return entry;
}

static inline int slist_empty(const struct slist_head *head) {
  return head->next == NULL;
}

static inline void slist_cut_splice(struct slist_head *head,
                                    struct slist_head *node,
                                    struct slist_head *new_head) {
  struct slist_head *tmp;
  tmp = head->next;
  head->next = node->next;
  node->next = new_head->next;
  new_head->next = tmp;
}

#endif /* LIBNVMMIO_SLIST_H */
