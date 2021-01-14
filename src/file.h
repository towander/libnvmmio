#ifndef LIBNVMMIO_FOPS_H
#define LIBNVMMIO_FOPS_H

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mmio.h"

typedef struct file_struct {
  mmio_t *mmio;
  int flags;
  int mode;
  off_t pos;
  unsigned long ino;
  pthread_mutex_t mutex;
} file_t;

struct fops_struct {
  int (*open)(const char *pathname, int flags, ...);
  int (*open64)(const char *pathname, int flags, ...);
  ssize_t (*read)(int fd, void *buf, size_t count);
  ssize_t (*write)(int fd, const void *buf, size_t count);
  ssize_t (*pread)(int fd, void *buf, size_t count, off_t pos);
  ssize_t (*pwrite)(int fd, const void *buf, size_t count, off_t pos);
  int (*fsync)(int fd);
  off_t (*lseek)(int fd, off_t offset, int whence);
  int (*truncate)(const char *path, off_t length);
  int (*ftruncate)(int fd, off_t length);
  int (*stat)(const char *pathname, struct stat *statbuf);
  int (*__fxstat)(int ver, int fd, struct stat *statbuf);
  int (*lstat)(const char *pathname, struct stat *statbuf);
  int (*close)(int fd);
};

#endif /* LIBNVMMIO_FOPS_H */
