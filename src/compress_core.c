#include "compress_core.h"
#include "huffman.h"
#include "bitio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tamaño máximo del árbol serializado:
   256 hojas × 9 bits + 255 internos × 1 bit = 2559 bits ≈ 320 bytes */
#define TREE_BUF_SIZE 512

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { *out_size = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    *out_size = sz > 0 ? (size_t)sz : 0;
    uint8_t *buf = malloc(*out_size > 0 ? *out_size : 1);
    if (*out_size > 0) {
        if (fread(buf, 1, *out_size, f) != *out_size) {
            fclose(f); free(buf); *out_size = 0; return NULL;
        }
    }
    fclose(f);
    return buf;
}

static void write_le16(uint8_t *buf, size_t *pos, uint16_t val) {
    buf[(*pos)++] = (uint8_t)(val & 0xFF);
    buf[(*pos)++] = (uint8_t)((val >> 8) & 0xFF);
}

static void write_le32(uint8_t *buf, size_t *pos, uint32_t val) {
    buf[(*pos)++] = (uint8_t)(val & 0xFF);
    buf[(*pos)++] = (uint8_t)((val >> 8)  & 0xFF);
    buf[(*pos)++] = (uint8_t)((val >> 16) & 0xFF);
    buf[(*pos)++] = (uint8_t)((val >> 24) & 0xFF);
}

static void write_le64(uint8_t *buf, size_t *pos, uint64_t val) {
    for (int i = 0; i < 8; i++)
        buf[(*pos)++] = (uint8_t)((val >> (i * 8)) & 0xFF);
}

CompressedBlock compress_one_file(const char *filepath, const char *base_dir) {
    CompressedBlock block = {NULL, NULL, 0};

    size_t file_size;
    uint8_t *file_data = read_file(filepath, &file_size);
    if (!file_data) return block;

    /* Ruta relativa al directorio base */
    const char *rel = filepath;
    size_t base_len = strlen(base_dir);
    if (strncmp(filepath, base_dir, base_len) == 0) {
        rel = filepath + base_len;
        while (*rel == '/') rel++;
    }
    uint16_t fname_len = (uint16_t)strlen(rel);

    /* Construcción del árbol y códigos */
    FreqTable freq;
    count_frequencies(file_data, file_size, freq);
    HuffNode *root = build_tree(freq);

    CodeTable codes;
    memset(codes, 0, sizeof(codes));
    if (root) generate_codes(root, codes);

    /* Serialización del árbol */
    uint8_t tree_buf[TREE_BUF_SIZE];
    memset(tree_buf, 0, TREE_BUF_SIZE);
    BitWriter tree_bw;
    bitwriter_init(&tree_bw, tree_buf, TREE_BUF_SIZE);
    if (root) serialize_tree(root, &tree_bw);
    bitwriter_flush(&tree_bw);
    uint32_t tree_size = (uint32_t)tree_bw.byte_pos;

    /* Conteo de bits comprimidos */
    uint64_t bit_count = 0;
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        if (freq[i] > 0 && codes[i].length > 0)
            bit_count += freq[i] * codes[i].length;
    }
    size_t compressed_bytes = (size_t)((bit_count + 7) / 8);

    /* Codificación de datos */
    uint8_t *comp_buf = calloc(compressed_bytes > 0 ? compressed_bytes : 1, 1);
    if (file_size > 0 && root) {
        BitWriter data_bw;
        bitwriter_init(&data_bw, comp_buf, compressed_bytes);
        for (size_t i = 0; i < file_size; i++)
            bitwriter_write_bits(&data_bw, codes[file_data[i]].value,
                                           codes[file_data[i]].length);
        bitwriter_flush(&data_bw);
    }

    /* Ensamblado del bloque binario */
    size_t block_size = 2 + fname_len + 8 + 8 + 4 + tree_size + compressed_bytes;
    uint8_t *bdata = malloc(block_size);
    size_t pos = 0;
    write_le16(bdata, &pos, fname_len);
    memcpy(bdata + pos, rel, fname_len); pos += fname_len;
    write_le64(bdata, &pos, (uint64_t)file_size);
    write_le64(bdata, &pos, bit_count);
    write_le32(bdata, &pos, tree_size);
    memcpy(bdata + pos, tree_buf, tree_size); pos += tree_size;
    memcpy(bdata + pos, comp_buf, compressed_bytes);

    block.filename = strdup(rel);
    block.data     = bdata;
    block.size     = block_size;

    free(file_data);
    free(comp_buf);
    free_tree(root);
    return block;
}

void free_compressed_block(CompressedBlock *block) {
    free(block->filename);
    free(block->data);
    block->filename = NULL;
    block->data     = NULL;
    block->size     = 0;
}
