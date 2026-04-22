#include "compress_core.h"
#include "timing.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>

#define MAX_FILES   4096
#define MAX_PATH    4096
#define NUM_THREADS 8

static int collect_txt(const char *dir, char **files, int *count) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_txt(path, files, count);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len >= 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
                if (*count < MAX_FILES)
                    files[(*count)++] = strdup(path);
            }
        }
    }
    closedir(d);
    return 0;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void fwrite_le32(FILE *f, uint32_t val) {
    uint8_t buf[4] = {
        (uint8_t)(val & 0xFF),
        (uint8_t)((val >> 8)  & 0xFF),
        (uint8_t)((val >> 16) & 0xFF),
        (uint8_t)((val >> 24) & 0xFF)
    };
    fwrite(buf, 1, 4, f);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <directorio_entrada> <archivo_salida.huff>\n", argv[0]);
        return 1;
    }
    const char *input_dir   = argv[1];
    const char *output_file = argv[2];

    char *files[MAX_FILES];
    int num_files = 0;
    collect_txt(input_dir, files, &num_files);
    qsort(files, (size_t)num_files, sizeof(char *), cmp_str);

    if (num_files == 0) {
        fprintf(stderr, "No se encontraron archivos .txt en %s\n", input_dir);
        return 1;
    }

    long long t_start = now_ms();

    /* Arreglo de resultados, uno por archivo */
    CompressedBlock *results = calloc(num_files, sizeof(CompressedBlock));
    int next = 0;
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);

    WorkerCtx ctx = {
        .files    = files,
        .base_dir = input_dir,
        .results  = results,
        .next     = &next,
        .total    = num_files,
        .mtx      = &mtx
    };

    /* Lanzar hilos */
    int nthreads = NUM_THREADS < num_files ? NUM_THREADS : num_files;
    pthread_t *threads = malloc(sizeof(pthread_t) * nthreads);
    for (int i = 0; i < nthreads; i++)
        pthread_create(&threads[i], NULL, worker, &ctx);

    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_destroy(&mtx);
    free(threads);

    /* Hilo principal escribe el archivo final en orden */
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", output_file);
        return 1;
    }
    fwrite("HUFF", 1, 4, out);
    uint8_t ver_reserved[4] = {0x01, 0x00, 0x00, 0x00};
    fwrite(ver_reserved, 1, 4, out);
    fwrite_le32(out, (uint32_t)num_files);

    for (int i = 0; i < num_files; i++) {
        if (results[i].data)
            fwrite(results[i].data, 1, results[i].size, out);
        free_compressed_block(&results[i]);
        free(files[i]);
    }
    fclose(out);
    free(results);

    printf("Tiempo total: %lld ms\n", now_ms() - t_start);
    return 0;
}