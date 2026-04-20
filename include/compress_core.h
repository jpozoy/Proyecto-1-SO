#ifndef COMPRESS_CORE_H
#define COMPRESS_CORE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char    *filename;
    uint8_t *data;
    size_t   size;
} CompressedBlock;

CompressedBlock compress_one_file(const char *filepath, const char *base_dir);
void            free_compressed_block(CompressedBlock *block);

#endif
