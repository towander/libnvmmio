#ifndef LIBNVMMIO_FILE_HASH_H
#define LIBNVMMIO_FILE_HASH_H

#include "mmio.h"
#include "file.h"

void init_file_hash(void);
mmio_t *get_mmio_hash(unsigned long ino);
mmio_t *put_mmio_hash(unsigned long ino, mmio_t *mmio);
void delete_mmio_hash(int fd, file_t *file);

#endif /* LIBNVMMIO_FILE_HASH_H */
