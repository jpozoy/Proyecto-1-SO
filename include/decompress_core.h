#ifndef DECOMPRESS_CORE_H
#define DECOMPRESS_CORE_H

#include <stdint.h>
#include <stddef.h>

int decompress_one_block(const uint8_t *block_data, size_t block_size,
                         const char *output_dir);

#endif
