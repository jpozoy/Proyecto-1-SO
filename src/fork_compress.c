#include "compress_core.h"
#include "timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#define MAX_FILES   4096
#define MAX_PATH    4096
#define MAX_WORKERS 8

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

/* Ruta del archivo temporal que produce el hijo que procesa el índice i */
static void tmp_path(char *out, size_t cap, int idx) {
    snprintf(out, cap, "/tmp/huff_fork_%d_%d.bin", (int)getpid(), idx);
}

/* Proceso hijo: comprime files[idx] a un tempfile y termina */
static void child_compress(const char *filepath, const char *base_dir,
                           int idx, int parent_pid) {
    CompressedBlock block = compress_one_file(filepath, base_dir);
    if (!block.data) {
        _exit(1);
    }
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/tmp/huff_fork_%d_%d.bin", parent_pid, idx);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free_compressed_block(&block);
        _exit(2);
    }
    if (fwrite(block.data, 1, block.size, f) != block.size) {
        fclose(f);
        free_compressed_block(&block);
        _exit(3);
    }
    fclose(f);
    free_compressed_block(&block);
    _exit(0);
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

    /* Lanzar pool de hijos */
    int parent_pid = (int)getpid();
    pid_t workers[MAX_WORKERS] = {0};
    int   worker_idx[MAX_WORKERS];    
    int   active = 0;
    int   next_to_launch = 0;
    int   finished = 0;
    int   failed = 0;

    while (finished < num_files) {
        while (active < MAX_WORKERS && next_to_launch < num_files) {
            int slot = -1;
            for (int i = 0; i < MAX_WORKERS; i++) {
                if (workers[i] == 0) { slot = i; break; }
            }
            if (slot < 0) break;

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                failed = 1;
                break;
            }
            if (pid == 0) {
                /* Hijo */
                child_compress(files[next_to_launch], input_dir,
                               next_to_launch, parent_pid);
            }
            workers[slot]    = pid;
            worker_idx[slot] = next_to_launch;
            next_to_launch++;
            active++;
        }

        if (failed) break;
        int status;
        pid_t done = wait(&status);
        if (done < 0) {
            if (errno == EINTR) continue;
            perror("wait");
            break;
        }
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i] == done) {
                workers[i] = 0;
                active--;
                finished++;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "Hijo falló en archivo %d (status=%d)\n",
                            worker_idx[i], status);
                    failed = 1;
                }
                break;
            }
        }
    }

    while (active > 0) {
        int status;
        if (wait(&status) < 0) break;
        active--;
    }

    if (failed) {
        fprintf(stderr, "Compresión abortada por error en un hijo\n");
        for (int i = 0; i < num_files; i++) {
            char p[MAX_PATH]; tmp_path(p, MAX_PATH, i);
            unlink(p);
            free(files[i]);
        }
        return 1;
    }

    /* Ensamblar archivo final */
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", output_file);
        return 1;
    }
    fwrite("HUFF", 1, 4, out);
    uint8_t ver_reserved[4] = {0x01, 0x00, 0x00, 0x00};
    fwrite(ver_reserved, 1, 4, out);
    fwrite_le32(out, (uint32_t)num_files);

    uint8_t copy_buf[64 * 1024];
    for (int i = 0; i < num_files; i++) {
        char path[MAX_PATH];
        tmp_path(path, MAX_PATH, i);
        FILE *tf = fopen(path, "rb");
        if (!tf) {
            fprintf(stderr, "Falta tempfile del archivo %d\n", i);
            fclose(out);
            return 1;
        }
        size_t n;
        while ((n = fread(copy_buf, 1, sizeof(copy_buf), tf)) > 0)
            fwrite(copy_buf, 1, n, out);
        fclose(tf);
        unlink(path);
        free(files[i]);
    }
    fclose(out);

    printf("Tiempo total: %lld ms\n", now_ms() - t_start);
    return 0;
}