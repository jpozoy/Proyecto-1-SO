#include "decompress_core.h"
#include "huffman.h"
#include "bitio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DC_MAX_PATH 4096

static void mkdirs(const char *path) {
    char tmp[DC_MAX_PATH];
    strncpy(tmp, path, DC_MAX_PATH - 1);
    tmp[DC_MAX_PATH - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static uint16_t rd_le16(const uint8_t *b, size_t *p) {
    uint16_t v = (uint16_t)(b[*p] | ((uint16_t)b[*p + 1] << 8));
    *p += 2;
    return v;
}

static uint32_t rd_le32(const uint8_t *b, size_t *p) {
    uint32_t v = (uint32_t)b[*p]
               | ((uint32_t)b[*p + 1] << 8)
               | ((uint32_t)b[*p + 2] << 16)
               | ((uint32_t)b[*p + 3] << 24);
    *p += 4;
    return v;
}

static uint64_t rd_le64(const uint8_t *b, size_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)b[*p + i] << (i * 8);
    *p += 8;
    return v;
}

int decompress_one_block(const uint8_t *block_data, size_t block_size,
                         const char *output_dir) {
    if (!block_data || block_size < 22) return -1;

    size_t pos = 0;
    uint16_t fname_len   = rd_le16(block_data, &pos);
    const char *fname    = (const char *)(block_data + pos); pos += fname_len;
    uint64_t orig_size   = rd_le64(block_data, &pos);
    uint64_t bit_count   = rd_le64(block_data, &pos);
    uint32_t tree_size   = rd_le32(block_data, &pos);

    const uint8_t *tree_data = block_data + pos; pos += tree_size;
    const uint8_t *comp_data = block_data + pos;

    /* Construir ruta de salida y crear directorios intermedios */
    char out_path[DC_MAX_PATH];
    snprintf(out_path, DC_MAX_PATH, "%s/%.*s", output_dir, (int)fname_len, fname);

    char dir_path[DC_MAX_PATH];
    strncpy(dir_path, out_path, DC_MAX_PATH - 1);
    dir_path[DC_MAX_PATH - 1] = '\0';
    char *slash = strrchr(dir_path, '/');
    if (slash) { *slash = '\0'; mkdirs(dir_path); }

    /* Decodificación */
    uint8_t *out_buf = NULL;
    if (orig_size > 0) {
        out_buf = malloc(orig_size);
        if (!out_buf) return -1;

        if (tree_size > 0) {
            BitReader tree_br;
            bitreader_init(&tree_br, tree_data, (size_t)tree_size * 8);
            HuffNode *root = deserialize_tree(&tree_br);

            BitReader data_br;
            bitreader_init(&data_br, comp_data, bit_count);

            HuffNode *node = root;
            for (uint64_t i = 0; i < orig_size; i++) {
                while (!node->is_leaf) {
                    int bit = bitreader_read_bit(&data_br);
                    node = (bit == 0) ? node->left : node->right;
                }
                out_buf[i] = node->byte_value;
                node = root;
            }
            free_tree(root);
        }
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) { free(out_buf); return -1; }
    if (orig_size > 0 && out_buf)
        fwrite(out_buf, 1, (size_t)orig_size, f);
    fclose(f);
    free(out_buf);
    return 0;
}
