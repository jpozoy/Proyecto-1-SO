#include "compress_core.h"
#include "timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>

#define MAX_FILES 4096
#define MAX_PATH  4096

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

    long long t_start = now_ms();

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
        CompressedBlock block = compress_one_file(files[i], input_dir);
        if (block.data)
            fwrite(block.data, 1, block.size, out);
        free_compressed_block(&block);
        free(files[i]);
    }

    fclose(out);
    printf("Tiempo total: %lld ms\n", now_ms() - t_start);
    return 0;
}
