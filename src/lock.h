#ifndef LIBNVMMIO_LOCK_H
#define LIBNVMMIO_LOCK_H

#include <pthread.h>

#include "debug.h"

#define MUTEX_LOCK(mutexp_)                                   \
  do {                                                        \
    if (__glibc_unlikely(pthread_mutex_lock(mutexp_) != 0)) { \
      HANDLE_ERROR("pthread_mutex_lock");                     \
    }                                                         \
  } while (0)

#define MUTEX_UNLOCK(mutexp_)                                   \
  do {                                                          \
    if (__glibc_unlikely(pthread_mutex_unlock(mutexp_) != 0)) { \
      HANDLE_ERROR("pthread_mutex_unlock");                     \
    }                                                           \
  } while (0)

#define RWLOCK_INIT(rwlockp_)                                         \
  do {                                                                \
    if (__glibc_unlikely(pthread_rwlock_init(rwlockp_, NULL) != 0)) { \
      HANDLE_ERROR("pthread_rwlock_init");                            \
    }                                                                 \
  } while (0)

#define RWLOCK_DESTROY(rwlockp_)                                   \
  do {                                                             \
    if (__glibc_unlikely(pthread_rwlock_destroy(rwlockp_) != 0)) { \
      HANDLE_ERROR("pthread_rwlock_destroy");                      \
    }                                                              \
  } while (0)

#define RWLOCK_READ_LOCK(rwlockp_)                                \
  do {                                                            \
    if (__glibc_unlikely(pthread_rwlock_rdlock(rwlockp_) != 0)) { \
      HANDLE_ERROR("pthread_rwlock_rdlock");                      \
    }                                                             \
  } while (0)

#define RWLOCK_READ_TRYLOCK(rwlockp_) (pthread_rwlock_tryrdlock(rwlockp_) == 0)

#define RWLOCK_WRITE_LOCK(rwlockp_)                               \
  do {                                                            \
    if (__glibc_unlikely(pthread_rwlock_wrlock(rwlockp_) != 0)) { \
      HANDLE_ERROR("pthread_rwlock_wrlock");                      \
    }                                                             \
  } while (0)

#define RWLOCK_WRITE_TRYLOCK(rwlockp_) (pthread_rwlock_trywrlock(rwlockp_) == 0)

#define RWLOCK_UNLOCK(rwlockp_)                                   \
  do {                                                            \
    if (__glibc_unlikely(pthread_rwlock_unlock(rwlockp_) != 0)) { \
      HANDLE_ERROR("pthread_rwlock_unlock");                      \
    }                                                             \
  } while (0)

#endif /* LIBNVMMIO_LOCK_H */
