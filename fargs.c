/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <libgen.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define HASH_SIZE SHA256_DIGEST_LENGTH
#define LOCK_FILE "/tmp/fargs.lock"
#define TEMP_FILE_TEMPLATE "/tmp/fargs_%d.tmp"
#define INDEX_FILE_TEMPLATE "/tmp/fargs_%d.idx"

char temp_filename[256];
char index_filename[256];
int quiet = 0;

void compute_hash(const char* path, unsigned char* hash) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const EVP_MD* md = EVP_sha256();

    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, path, strlen(path));
    EVP_DigestFinal_ex(ctx, hash, NULL);
    EVP_MD_CTX_free(ctx);
}

int hash_exists(FILE* index_file, unsigned char* hash) {
    rewind(index_file);
    unsigned char buffer[HASH_SIZE];
    while (fread(buffer, HASH_SIZE, 1, index_file)) {
        if (memcmp(buffer, hash, HASH_SIZE) == 0) {
            return 1;
        }
    }
    return 0;
}

int process_path(const char* path, FILE* temp_file, FILE* index_file) {
    struct stat st;
    if (stat(path, &st) == -1) {
        fprintf(stderr, "Path does not exist: %s\n", path);
        return 0;
    }

    char* abs_path = realpath(path, NULL);
    if (!abs_path) {
        fprintf(stderr, "Invalid path: %s\n", path);
        return 0;
    }

    unsigned char hash[HASH_SIZE];
    compute_hash(abs_path, hash);

    if (hash_exists(index_file, hash)) {
        free(abs_path);
        return 0;
    }

    fprintf(temp_file, "%s\n", abs_path);
    fwrite(hash, HASH_SIZE, 1, index_file);
    free(abs_path);
    return 1;
}

int lock_file_exists() {
    return access(LOCK_FILE, F_OK) == 0;
}

int create_lock_file(int force) {
    if (force && lock_file_exists()) {
        if (unlink(LOCK_FILE) == -1) {
            perror("Failed to remove lock file");
            return -1;
        }
    }
    int fd = open(LOCK_FILE, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "Error: Lock file exists\n");
        } else {
            perror("Failed to create lock file");
        }
        return -1;
    }
    close(fd);
    return 0;
}

void remove_lock_file() {
    unlink(LOCK_FILE);
}

int append_mode(int argc, char** argv, int force, int quiet) {
    if (lock_file_exists() && !force) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }

    if (create_lock_file(force) != 0) {
        return -1;
    }

    FILE* temp_file = fopen(temp_filename, "a");
    if (!temp_file) {
        perror("Failed to open temp file");
        return -1;
    }

    FILE* index_file = fopen(index_filename, "a+");
    if (!index_file) {
        perror("Failed to open index file");
        fclose(temp_file);
        return -1;
    }

    int count = 0;

    // 1. Обработка аргументов командной строки
    for (int i = 0; i < argc; i++) {
        glob_t glob_result;
        if (glob(argv[i], GLOB_TILDE | GLOB_MARK, NULL, &glob_result) == 0) {
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                count += process_path(glob_result.gl_pathv[j], temp_file, index_file);
            }
            globfree(&glob_result);
        }
    }

    // 2. Чтение из stdin ТОЛЬКО если он не пустой
    if (!isatty(fileno(stdin))) {
        char* line = NULL;
        size_t len = 0;
        while (getline(&line, &len, stdin) != -1) {
            line[strcspn(line, "\n")] = '\0';
            count += process_path(line, temp_file, index_file);
        }
        free(line);
    }

    fclose(temp_file);
    fclose(index_file);

    if (!quiet) {
        struct stat st;
        int total = 0;
        if (stat(index_filename, &st) == 0) {
            total = st.st_size / HASH_SIZE;
        }
        printf("%d paths added / %d paths total\n", count, total);
    }

    remove_lock_file();
    return 0;
}

int replace_mode(int argc, char** argv, int force, int quiet) {
    if (lock_file_exists() && !force) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }

    if (create_lock_file(force) != 0) {
        return -1;
    }

    FILE* temp_file = fopen(temp_filename, "w");
    if (!temp_file) {
        perror("Failed to create temp file");
        remove_lock_file();
        return -1;
    }
    fclose(temp_file);

    FILE* index_file = fopen(index_filename, "wb");
    if (!index_file) {
        perror("Failed to create index file");
        remove_lock_file();
        return -1;
    }
    fclose(index_file);
    remove_lock_file();

    int result = append_mode(argc, argv, force, quiet);
    return result;
}

int out_mode(int sort_flag, int clear_flag, int force) {
    if (lock_file_exists() && !force) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }

    if (clear_flag && create_lock_file(force) != 0) {
        return -1;
    }

    FILE* temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        perror("Failed to open temp file");
        if (clear_flag)
            remove_lock_file();
        return -1;
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    if (sort_flag) {
        char** lines = NULL;
        size_t count = 0;

        while ((read = getline(&line, &len, temp_file)) != -1) {
            lines = realloc(lines, (count + 1) * sizeof(char*));
            lines[count] = strdup(line);
            count++;
        }

        qsort(lines, count, sizeof(char*), (int (*)(const void*, const void*))strcmp);

        for (size_t i = 0; i < count; i++) {
            printf("%s", lines[i]);
            free(lines[i]);
        }
        free(lines);
    } else {
        while ((read = getline(&line, &len, temp_file)) != -1) {
            printf("%s", line);
        }
    }

    free(line);
    fclose(temp_file);

    if (clear_flag) {
        unlink(temp_filename);
        unlink(index_filename);
        remove_lock_file();
    }

    return 0;
}

int clear_mode(int force) {
    if (lock_file_exists() && !force) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }

    if (create_lock_file(force) != 0) {
        return -1;
    }

    unlink(temp_filename);
    unlink(index_filename);
    remove_lock_file();
    return 0;
}

int unlock_mode() {
    if (!lock_file_exists()) {
        printf("No lock file found\n");
        return 0;
    }

    printf("Other fargs acquired lock. Release existing lock? [Y/N] ");
    char response = getchar();
    if (response == 'Y' || response == 'y') {
        if (unlink(LOCK_FILE) == 0) {
            printf("Lock file removed\n");
        } else {
            perror("Failed to remove lock file");
            return -1;
        }
    }
    return 0;
}

void print_help() {
    printf("Usage: fargs [options] <command> [paths...]\n"
           "Commands:\n"
           "  append, a   Add paths to the selection\n"
           "  replace, r  Replace the selection with new paths\n"
           "  out, o      Output the selection\n"
           "  clear, c    Clear the selection\n"
           "  unlock, u   Remove the lock file\n"
           "  help        Show this help\n"
           "Options:\n"
           "  -q          Suppress info messages\n"
           "  -s          Sort files in selection on output\n"
           "  -c          Clear selection after output\n"
           "  -f          Force operation (ignore lock)\n"
           "  -h          Show this help\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    int uid = getuid();
    snprintf(temp_filename, sizeof(temp_filename), TEMP_FILE_TEMPLATE, uid);
    snprintf(index_filename, sizeof(index_filename), INDEX_FILE_TEMPLATE, uid);

    int opt;
    int force = 0, sort_flag = 0, clear_flag = 0;

    while ((opt = getopt(argc, argv, "qsfch")) != -1) {
        switch (opt) {
            case 'q':
                quiet = 1;
                break;
            case 's':
                sort_flag = 1;
                break;
            case 'f':
                force = 1;
                break;
            case 'c':
                clear_flag = 1;
                break;
            case 'h':
                print_help();
                return 0;
            default:
                fprintf(stderr, "Invalid option\n");
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No command specified\n");
        print_help();
        return 1;
    }

    char* command = argv[optind];
    argc -= optind + 1;
    argv += optind + 1;

    if (strcmp(command, "append") == 0 || strcmp(command, "a") == 0) {
        return append_mode(argc, argv, force, quiet);
    } else if (strcmp(command, "replace") == 0 || strcmp(command, "r") == 0) {
        return replace_mode(argc, argv, force, quiet);
    } else if (strcmp(command, "out") == 0 || strcmp(command, "o") == 0) {
        return out_mode(sort_flag, clear_flag, force);
    } else if (strcmp(command, "clear") == 0 || strcmp(command, "c") == 0) {
        return clear_mode(force);
    } else if (strcmp(command, "unlock") == 0 || strcmp(command, "u") == 0) {
        return unlock_mode();
    } else {
        fprintf(stderr, "Invalid command\n");
        return 1;
    }
}
