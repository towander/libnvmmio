#ifndef LIBNVMMIO_DEBUG_H
#define LIBNVMMIO_DEBUG_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GiB (1UL << 30)
#define MiB (1UL << 20)
#define KiB (1UL << 10)
#define SIZE_FORMAT "%g%s"

#ifdef DEBUG
#define PRINT(fmt, ...)                                               \
  do {                                                                \
    fprintf(stderr, "%ld [%s:%d:%s] " fmt "\n", (long)pthread_self(), \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__);             \
  } while (0)
#else /* DEBUG */
#define PRINT(fmt, ...) \
  do {                  \
  } while (0)
#endif /* DEBUG */

#define HANDLE_ERROR(fmt, ...)                                                \
  do {                                                                        \
    fprintf(stderr, "[%s:%d:%s] " fmt ": %s\n", __FILE__, __LINE__, __func__, \
            ##__VA_ARGS__, strerror(errno));                                  \
    exit(EXIT_FAILURE);                                                       \
  } while (0)

#ifdef DEBUG
static inline char *get_readable_size(unsigned long size) {
  char *buf;
  double s;

  buf = (char *)malloc(20);
  if (buf == NULL) {
    HANDLE_ERROR("malloc");
  }

  if (size >= GiB) {
    s = (double)size / GiB;
    sprintf(buf, SIZE_FORMAT, s, "GiB");
  } else if (size >= MiB) {
    s = (double)size / MiB;
    sprintf(buf, SIZE_FORMAT, s, "MiB");
  } else if (size >= KiB) {
    s = (double)size / KiB;
    sprintf(buf, SIZE_FORMAT, s, "KiB");
  } else {
    s = size;
    sprintf(buf, SIZE_FORMAT, s, "Bytes");
  }
  return buf;
}
#endif /* DEBUG */

#endif /* LIBNVMMIO_DEBUG_H */
