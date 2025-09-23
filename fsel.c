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
#define LOCK_FILE_TEMPLATE "/tmp/fsel_%d.lock"
#define TEMP_FILE_TEMPLATE "/tmp/fsel_%d.tmp"
#define INDEX_FILE_TEMPLATE "/tmp/fsel_%d.idx"

// Command line flags
#define FORCE_FLAG 0x01
#define QUIET_FLAG 0x02
#define SORT_FLAG 0x04
#define CLEAR_FLAG 0x08
#define REPLACE_FLAG 0x10
#define UNLOCK_FLAG 0x20
#define VALIDATE_FLAG 0x40

char lock_filename[256];
char temp_filename[256];
char index_filename[256];

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
    return access(lock_filename, F_OK) == 0;
}

int create_lock_file(int force) {
    if (force && lock_file_exists()) {
        if (unlink(lock_filename) == -1) {
            perror("Failed to remove lock file");
            return -1;
        }
    }
    int fd = open(lock_filename, O_WRONLY | O_CREAT | O_EXCL, 0600);
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
    unlink(lock_filename);
}

char* safe_strdup(const char* str) {
    char* new_str = strdup(str);
    if (!new_str) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }
    return new_str;
}

int add_mode(int argc, char** argv, int flags) {
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (create_lock_file(flags & FORCE_FLAG) != 0) {
        return -1;
    }
    
    // Handle replace flag
    if (flags & REPLACE_FLAG) {
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
    if (!(flags & QUIET_FLAG)) {
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

int list_mode(int _, char** __, int flags) {
    (void)_;
    (void)__;
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (flags & CLEAR_FLAG && create_lock_file(flags & FORCE_FLAG) != 0) {
        return -1;
    }
    if (access(temp_filename, F_OK) == -1) {
        return 0;
    }
    FILE* temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        perror("Failed to open temp file");
        if (flags & CLEAR_FLAG)
            remove_lock_file();
        return -1;
    }
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    if (flags & SORT_FLAG) {
        char** lines = NULL;
        size_t count = 0;
        while ((read = getline(&line, &len, temp_file)) != -1) {
            lines = realloc(lines, (count + 1) * sizeof(char*));
            lines[count] = safe_strdup(line);
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
    if (flags & CLEAR_FLAG) {
        unlink(temp_filename);
        unlink(index_filename);
        remove_lock_file();
    }
    return 0;
}

int clear_mode(int _, char** __, int flags) {
    (void)_;
    (void)__;
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (create_lock_file(flags & FORCE_FLAG) != 0) {
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
    printf("Other instance of \"fsel\" acquired lock. Release existing lock? [Y/N] ");
    char response = getchar();
    if (response == 'Y' || response == 'y') {
        if (unlink(lock_filename) == 0) {
            printf("Lock file removed\n");
        } else {
            perror("Failed to remove lock file");
            return -1;
        }
    }
    return 0;
}

int validate_mode(int _, char** __, int flags) {
    (void)_;
    (void)__;
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (access(temp_filename, F_OK) == -1) {
        return 0;
    }
    FILE* temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        perror("Failed to open temp file");
        return -1;
    }
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    int valid_count = 0;
    int invalid_count = 0;

    while ((read = getline(&line, &len, temp_file)) != -1) {
        line[strcspn(line, "\n")] = '\0';
        struct stat st;
        if (stat(line, &st) == 0) {
            printf("✓ %s\n", line);
            valid_count++;
        } else {
            printf("✗ %s\n", line);
            invalid_count++;
        }
    }
    free(line);
    fclose(temp_file);

    if (!(flags & QUIET_FLAG)) {
        printf("Total: %d valid, %d invalid\n", valid_count, invalid_count);
    }

    return (invalid_count > 0) ? 1 : 0;
}

int print_help() {
    printf("Usage: fsel [options] [paths...]\n"
           "Options:\n"
           "  -q          Suppress info messages\n"
           "  -s          Sort files in selection on output\n"
           "  -c          Clear selection after output / Clear selection\n"
           "  -f          Force operation (ignore lock)\n"
           "  -r          Replace the selection with new paths\n"
           "  -u          Remove the lock file\n"
           "  -v          Validate the selection\n"
           "  -h          Show this help\n"
           "\n"
           "When no paths are provided, list mode is used by default.\n"
           "When paths are provided without -r, they are added to the selection.\n");
    return 0;
}

int main(int argc, char** argv) {
    int uid = getuid();
    snprintf(temp_filename, sizeof(temp_filename), TEMP_FILE_TEMPLATE, uid);
    snprintf(index_filename, sizeof(index_filename), INDEX_FILE_TEMPLATE, uid);
    snprintf(lock_filename, sizeof(index_filename), LOCK_FILE_TEMPLATE, uid);

    int opt;
    int flags = 0;
    while ((opt = getopt(argc, argv, "qscfruvh")) != -1) {
        switch (opt) {
            case 'q':
                flags |= QUIET_FLAG;
                break;
            case 's':
                flags |= SORT_FLAG;
                break;
            case 'c':
                flags |= CLEAR_FLAG;
                break;
            case 'f':
                flags |= FORCE_FLAG;
                break;
            case 'r':
                flags |= REPLACE_FLAG;
                break;
            case 'u':
                flags |= UNLOCK_FLAG;
                break;
            case 'v':
                flags |= VALIDATE_FLAG;
                break;
            case 'h':
                return print_help();
            default:
                fprintf(stderr, "Invalid option\n");
                return EXIT_FAILURE;
        }
    }

    // Handle standalone flags
    if (flags & UNLOCK_FLAG) {
        return unlock_mode();
    }
    
    if (flags & VALIDATE_FLAG) {
        return validate_mode(0, NULL, flags);
    }
    
    if (flags & CLEAR_FLAG && optind >= argc) {
        return clear_mode(0, NULL, flags);
    }

    if (optind >= argc) {
        // When no paths are provided, default to list mode
        return list_mode(0, NULL, flags);
    }

    // When paths are provided, use add mode
    return add_mode(argc - optind, argv + optind, flags);

    return EXIT_SUCCESS;
}
