#include "decompress_core.h"
#include "timing.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>

#define MAX_PATH    4096
#define NUM_THREADS 8

static void xfread(void *ptr, size_t n, FILE *f) {
    size_t r = fread(ptr, 1, n, f);
    (void)r;
}
static uint16_t frd_le16(FILE *f) {
    uint8_t b[2] = {0}; xfread(b, 2, f);
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}
static uint32_t frd_le32(FILE *f) {
    uint8_t b[4] = {0}; xfread(b, 4, f);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint64_t frd_le64(FILE *f) {
    uint8_t b[8] = {0}; xfread(b, 8, f);
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)b[i] << (i * 8);
    return v;
}
static void pack_le16(uint8_t *buf, size_t *p, uint16_t v) {
    buf[(*p)++] = v & 0xFF; buf[(*p)++] = (v >> 8) & 0xFF;
}
static void pack_le32(uint8_t *buf, size_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) buf[(*p)++] = (v >> (i * 8)) & 0xFF;
}
static void pack_le64(uint8_t *buf, size_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) buf[(*p)++] = (v >> (i * 8)) & 0xFF;
}

typedef struct {
    uint8_t *data;
    size_t   size;
} Block;

typedef struct {
    Block           *blocks;
    int              total;
    int             *next;
    const char      *output_dir;
    pthread_mutex_t *mtx;
} WorkerCtx;

static void *worker(void *arg) {
    WorkerCtx *ctx = (WorkerCtx *)arg;
    while (1) {
        pthread_mutex_lock(ctx->mtx);
        int idx = *(ctx->next);
        if (idx >= ctx->total) {
            pthread_mutex_unlock(ctx->mtx);
            break;
        }
        (*(ctx->next))++;
        pthread_mutex_unlock(ctx->mtx);

        decompress_one_block(ctx->blocks[idx].data,
                             ctx->blocks[idx].size,
                             ctx->output_dir);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo.huff> <directorio_salida>\n", argv[0]);
        return 1;
    }
    const char *input_file = argv[1];
    const char *output_dir = argv[2];

    FILE *f = fopen(input_file, "rb");
    if (!f) { fprintf(stderr, "Error: no se pudo abrir %s\n", input_file); return 1; }

    char magic[4] = {0};
    xfread(magic, 4, f);
    if (memcmp(magic, "HUFF", 4) != 0) {
        fprintf(stderr, "Error: magic number invalido\n");
        fclose(f); return 1;
    }
    uint8_t version = 0; xfread(&version, 1, f);
    if (version != 1) {
        fprintf(stderr, "Error: version no soportada (%d)\n", version);
        fclose(f); return 1;
    }
    uint8_t reserved[3]; xfread(reserved, 3, f);
    uint32_t num_files = frd_le32(f);

    mkdir(output_dir, 0755);

    long long t_start = now_ms();

    Block *blocks = calloc(num_files, sizeof(Block));
    for (uint32_t i = 0; i < num_files; i++) {
        uint16_t fname_len  = frd_le16(f);
        uint8_t  fname[MAX_PATH];
        xfread(fname, fname_len, f);
        uint64_t orig_size  = frd_le64(f);
        uint64_t bit_count  = frd_le64(f);
        uint32_t tree_size  = frd_le32(f);
        size_t   comp_bytes = (size_t)((bit_count + 7) / 8);
        size_t block_size = 2 + fname_len + 8 + 8 + 4 + tree_size + comp_bytes;

        uint8_t *block = malloc(block_size);
        size_t p = 0;
        pack_le16(block, &p, fname_len);
        memcpy(block + p, fname, fname_len); p += fname_len;
        pack_le64(block, &p, orig_size);
        pack_le64(block, &p, bit_count);
        pack_le32(block, &p, tree_size);
        if (tree_size > 0)  { xfread(block + p, tree_size,  f); p += tree_size; }
        if (comp_bytes > 0) { xfread(block + p, comp_bytes, f); }

        blocks[i].data = block;
        blocks[i].size = block_size;
    }
    fclose(f);

    int next = 0;
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);

    WorkerCtx ctx = {
        .blocks     = blocks,
        .total      = (int)num_files,
        .next       = &next,
        .output_dir = output_dir,
        .mtx        = &mtx
    };

    int nthreads = NUM_THREADS < (int)num_files ? NUM_THREADS : (int)num_files;
    pthread_t *threads = malloc(sizeof(pthread_t) * nthreads);
    for (int i = 0; i < nthreads; i++)
        pthread_create(&threads[i], NULL, worker, &ctx);
    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_destroy(&mtx);
    free(threads);

    for (uint32_t i = 0; i < num_files; i++)
        free(blocks[i].data);
    free(blocks);

    printf("Tiempo total: %lld ms\n", now_ms() - t_start);
    return 0;
}