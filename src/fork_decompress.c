#include "decompress_core.h"
#include "timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#define MAX_PATH    4096
#define MAX_WORKERS 8

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

static void tmp_path(char *out, size_t cap, int parent_pid, int idx) {
    snprintf(out, cap, "/tmp/huff_dec_%d_%d.bin", parent_pid, idx);
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
        fprintf(stderr, "Error: magic number inválido\n");
        fclose(f); return 1;
    }
    uint8_t version = 0; xfread(&version, 1, f);
    if (version != 1) {
        fprintf(stderr, "Error: versión no soportada (%d)\n", version);
        fclose(f); return 1;
    }
    uint8_t reserved[3]; xfread(reserved, 3, f);
    uint32_t num_files = frd_le32(f);

    mkdir(output_dir, 0755);

    long long t_start = now_ms();

    //Se parte el .huff en tempfiles, uno por bloque
    int parent_pid = (int)getpid();
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

        char path[MAX_PATH];
        tmp_path(path, MAX_PATH, parent_pid, (int)i);
        FILE *tf = fopen(path, "wb");
        if (!tf) { free(block); fclose(f); return 1; }
        fwrite(block, 1, block_size, tf);
        fclose(tf);
        free(block);
    }
    fclose(f);

    //Pool de hijos, cada uno descomprime un tempfile  
    pid_t workers[MAX_WORKERS] = {0};
    int   active = 0;
    int   next_to_launch = 0;
    int   finished = 0;

    while (finished < (int)num_files) {
        while (active < MAX_WORKERS && next_to_launch < (int)num_files) {
            int slot = -1;
            for (int i = 0; i < MAX_WORKERS; i++) {
                if (workers[i] == 0) { slot = i; break; }
            }
            if (slot < 0) break;

            pid_t pid = fork();
            if (pid < 0) { perror("fork"); break; }
            if (pid == 0) {
                // Hijo lee el tempfile y descomprime el bloque 
                char path[MAX_PATH];
                tmp_path(path, MAX_PATH, parent_pid, next_to_launch);
                FILE *tf = fopen(path, "rb");
                if (!tf) _exit(1);
                fseek(tf, 0, SEEK_END);
                long sz = ftell(tf);
                rewind(tf);
                uint8_t *block = malloc(sz);
                if (fread(block, 1, sz, tf) != (size_t)sz) {
                    free(block); fclose(tf); _exit(2);
                }
                fclose(tf);
                int rc = decompress_one_block(block, (size_t)sz, output_dir);
                free(block);
                unlink(path);
                _exit(rc == 0 ? 0 : 3);
            }
            workers[slot] = pid;
            next_to_launch++;
            active++;
        }

        int status;
        pid_t done = wait(&status);
        if (done < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i] == done) {
                workers[i] = 0;
                active--;
                finished++;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    fprintf(stderr, "Hijo falló (status=%d)\n", status);
                break;
            }
        }
    }

    while (active > 0) {
        int status;
        if (wait(&status) < 0) break;
        active--;
    }

    printf("Tiempo total: %lld ms\n", now_ms() - t_start);
    return 0;
}