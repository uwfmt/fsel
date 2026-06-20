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
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define HASH_SIZE SHA256_DIGEST_LENGTH

// Command line flags
#define FORCE_FLAG 0x01
#define QUIET_FLAG 0x02
#define SORT_FLAG 0x04
#define CLEAR_FLAG 0x08
#define REPLACE_FLAG 0x10
#define UNLOCK_FLAG 0x20
#define VALIDATE_FLAG 0x40
#define LONG_FORMAT_FLAG 0x80
#define DELETE_FLAG 0x100

char lock_filename[PATH_MAX];
char temp_filename[PATH_MAX];
char index_filename[PATH_MAX];

int is_active_line(const char* line) {
    return line != NULL && line[0] == '/';
}

int is_zero_hash(const unsigned char* hash) {
    for (int i = 0; i < HASH_SIZE; i++) {
        if (hash[i] != 0) {
            return 0;
        }
    }
    return 1;
}

void count_storage(int* active, int* tombstones) {
    *active = 0;
    *tombstones = 0;
    if (access(temp_filename, F_OK) == -1) {
        return;
    }
    FILE* temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        return;
    }
    char* line = NULL;
    size_t len = 0;
    while (getline(&line, &len, temp_file) != -1) {
        if (is_active_line(line)) {
            (*active)++;
        } else if (len > 0) {
            (*tombstones)++;
        }
    }
    free(line);
    fclose(temp_file);
}

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
        if (is_zero_hash(buffer)) {
            continue;
        }
        if (memcmp(buffer, hash, HASH_SIZE) == 0) {
            return 1;
        }
    }
    return 0;
}

int reuse_tombstone_slot(const char* abs_path, const unsigned char* hash) {
    FILE* temp_file = fopen(temp_filename, "r+");
    if (!temp_file) {
        return 0;
    }
    FILE* index_file = fopen(index_filename, "r+");
    if (!index_file) {
        fclose(temp_file);
        return 0;
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    off_t offset = 0;
    int line_index = 0;
    size_t path_len = strlen(abs_path);

    while ((read = getline(&line, &len, temp_file)) != -1) {
        size_t line_len = (size_t)read;
        if (!is_active_line(line)) {
            if (path_len + 1 <= line_len) {
                char* buf = malloc(line_len);
                if (!buf) {
                    perror("Failed to allocate memory");
                    free(line);
                    fclose(temp_file);
                    fclose(index_file);
                    return 0;
                }
                memcpy(buf, abs_path, path_len);
                buf[path_len] = '\n';
                if (line_len > path_len + 1) {
                    memset(buf + path_len + 1, ' ', line_len - path_len - 1);
                }
                fseek(temp_file, offset, SEEK_SET);
                if (fwrite(buf, 1, line_len, temp_file) != line_len) {
                    perror("Failed to reuse tombstone slot");
                    free(buf);
                    free(line);
                    fclose(temp_file);
                    fclose(index_file);
                    return 0;
                }
                free(buf);
                fseek(index_file, (long)line_index * HASH_SIZE, SEEK_SET);
                if (fwrite(hash, HASH_SIZE, 1, index_file) != 1) {
                    perror("Failed to write hash for reused slot");
                    free(line);
                    fclose(temp_file);
                    fclose(index_file);
                    return 0;
                }
                free(line);
                fclose(temp_file);
                fclose(index_file);
                return 1;
            }
        }
        offset += read;
        line_index++;
    }
    free(line);
    fclose(temp_file);
    fclose(index_file);
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
    if (reuse_tombstone_slot(abs_path, hash)) {
        free(abs_path);
        return 1;
    }
    fprintf(temp_file, "%s\n", abs_path);
    if (fwrite(hash, HASH_SIZE, 1, index_file) != 1) {
        fprintf(stderr, "Failed to write hash for: %s\n", abs_path);
        free(abs_path);
        return 0;
    }
    free(abs_path);
    return 1;
}

int lock_file_exists() {
    return access(lock_filename, F_OK) == 0;
}

// Lock for the user
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

int compare_strings(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

// Function to format time like ls -l
void format_time(char* buffer, size_t size, time_t time_val) {
    time_t now = time(NULL);
    // If file is older than 6 months, show year instead of time
    if (now - time_val > 180 * 24 * 60 * 60) {
        strftime(buffer, size, "%b %d  %Y", localtime(&time_val));
    } else {
        strftime(buffer, size, "%b %d %H:%M", localtime(&time_val));
    }
}

// Function to print file info in ls -l format
void print_file_info(const char* path) {
    struct stat st;
    if (lstat(path, &st) == -1) {
        printf("Could not stat %s\n", path);
        return;
    }

    // File type and permissions
    char perms[11];
    if (S_ISLNK(st.st_mode)) {
        perms[0] = 'l';
    } else if (S_ISDIR(st.st_mode)) {
        perms[0] = 'd';
    } else if (S_ISCHR(st.st_mode)) {
        perms[0] = 'c';
    } else if (S_ISBLK(st.st_mode)) {
        perms[0] = 'b';
    } else if (S_ISFIFO(st.st_mode)) {
        perms[0] = 'p';
    } else if (S_ISSOCK(st.st_mode)) {
        perms[0] = 's';
    } else {
        perms[0] = '-';
    }
    perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
    perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
    perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
    perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
    perms[10] = '\0';

    // Get user and group names
    struct passwd* pwd = getpwuid(st.st_uid);
    struct group* grp = getgrgid(st.st_gid);
    char* user = pwd ? pwd->pw_name : "?";
    char* group = grp ? grp->gr_name : "?";

    // Format time
    char time_str[20];
    format_time(time_str, sizeof(time_str), st.st_mtime);

    // Print in ls -l format with aligned columns
    printf("%s %3ld %-8s %-8s %8ld %s %s", perms, (long)st.st_nlink, user, group, (long)st.st_size, time_str, path);

    // For symlinks, show the target
    if (S_ISLNK(st.st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(path, link_target, sizeof(link_target) - 1);
        if (len != -1) {
            link_target[len] = '\0';
            printf(" -> %s", link_target);
        }
    }
    printf("\n");
}

// Add new path to selection
int add_mode(int argc, char** argv, int flags) {
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (create_lock_file(flags & FORCE_FLAG) != 0) {
        return -1;
    }

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
    // 1. Process command line arguments
    for (int i = 0; i < argc; i++) {
        glob_t glob_result;
        if (glob(argv[i], GLOB_TILDE | GLOB_MARK, NULL, &glob_result) == 0) {
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                count += process_path(glob_result.gl_pathv[j], temp_file, index_file);
            }
            globfree(&glob_result);
        }
    }
    // 2. Read from stdin ONLY if it's not empty
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
        int active = 0;
        int tombstones = 0;
        count_storage(&active, &tombstones);
        (void)tombstones;
        printf("%d paths added / %d paths total\n", count, active);
    }
    remove_lock_file();
    return 0;
}

// List files like in "ls -l"
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
            if (!is_active_line(line)) {
                continue;
            }
            char** tmp = realloc(lines, (count + 1) * sizeof(char*));
            if (!tmp) {
                perror("Failed to allocate memory for lines");
                for (size_t i = 0; i < count; i++) {
                    free(lines[i]);
                }
                free(lines);
                free(line);
                fclose(temp_file);
                if (flags & CLEAR_FLAG)
                    remove_lock_file();
                return -1;
            }
            lines = tmp;
            lines[count] = safe_strdup(line);
            count++;
        }
        qsort(lines, count, sizeof(char*), compare_strings);
        for (size_t i = 0; i < count; i++) {
            if (flags & LONG_FORMAT_FLAG) {
                lines[i][strcspn(lines[i], "\n")] = '\0';
                print_file_info(lines[i]);
            } else {
                printf("%s", lines[i]);
            }
            free(lines[i]);
        }
        free(lines);
    } else {
        while ((read = getline(&line, &len, temp_file)) != -1) {
            if (!is_active_line(line)) {
                continue;
            }
            if (flags & LONG_FORMAT_FLAG) {
                line[strcspn(line, "\n")] = '\0';
                print_file_info(line);
            } else {
                printf("%s", line);
            }
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

// Clear selection completely
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

// Release possibly stalled lock
int unlock_mode(int _, char** __, int flags) {
    (void)_;
    (void)__;
    if (!lock_file_exists()) {
        printf("No lock file found\n");
        return 0;
    }
    char response = 'Y';
    if (!(flags & FORCE_FLAG)) {
        printf("Other instance of \"fsel\" acquired lock. Release existing lock? [Y/N] ");
        response = getchar();
    }
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

// Check that path are still exist
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
        if (!is_active_line(line)) {
            continue;
        }
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

// Remove empty lines frov previous deletions
int compact_storage(void) {
    char temp_new[PATH_MAX];
    char index_new[PATH_MAX];
    int ret = snprintf(temp_new, sizeof(temp_new), "%s.new", temp_filename);
    if (ret < 0 || ret >= (int)sizeof(temp_new)) {
        fprintf(stderr, "Error: Path too long for temp new file\n");
        return -1;
    }
    ret = snprintf(index_new, sizeof(index_new), "%s.new", index_filename);
    if (ret < 0 || ret >= (int)sizeof(index_new)) {
        fprintf(stderr, "Error: Path too long for index new file\n");
        return -1;
    }

    FILE* temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        perror("Failed to open temp file");
        return -1;
    }
    FILE* index_file = fopen(index_filename, "rb");
    if (!index_file) {
        perror("Failed to open index file");
        fclose(temp_file);
        return -1;
    }
    FILE* temp_out = fopen(temp_new, "w");
    if (!temp_out) {
        perror("Failed to create temp new file");
        fclose(temp_file);
        fclose(index_file);
        return -1;
    }
    FILE* index_out = fopen(index_new, "wb");
    if (!index_out) {
        perror("Failed to create index new file");
        fclose(temp_file);
        fclose(index_file);
        fclose(temp_out);
        unlink(temp_new);
        return -1;
    }

    char* line = NULL;
    size_t len = 0;
    unsigned char hash[HASH_SIZE];
    int rc = 0;

    while (getline(&line, &len, temp_file) != -1) {
        if (!is_active_line(line)) {
            if (fread(hash, HASH_SIZE, 1, index_file) != 1) {
                fprintf(stderr, "Index file out of sync with temp file\n");
                rc = -1;
                break;
            }
            continue;
        }
        fprintf(temp_out, "%s", line);
        if (fread(hash, HASH_SIZE, 1, index_file) != 1) {
            fprintf(stderr, "Index file out of sync with temp file\n");
            rc = -1;
            break;
        }
        if (fwrite(hash, HASH_SIZE, 1, index_out) != 1) {
            perror("Failed to write hash");
            rc = -1;
            break;
        }
    }

    free(line);
    fclose(temp_file);
    fclose(index_file);
    fclose(temp_out);
    fclose(index_out);

    if (rc != 0) {
        unlink(temp_new);
        unlink(index_new);
        return -1;
    }

    if (rename(temp_new, temp_filename) != 0) {
        perror("Failed to rename temp file");
        unlink(temp_new);
        unlink(index_new);
        return -1;
    }
    if (rename(index_new, index_filename) != 0) {
        perror("Failed to rename index file");
        return -1;
    }
    return 0;
}

int maybe_compact(void) {
    int active = 0;
    int tombstones = 0;
    count_storage(&active, &tombstones);
    int total = active + tombstones;
    if (total >= 100 && tombstones * 10 > total * 3) {
        return compact_storage();
    }
    return 0;
}

char* resolve_delete_key(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        char* abs_path = realpath(path, NULL);
        if (!abs_path) {
            fprintf(stderr, "Invalid path: %s\n", path);
            return NULL;
        }
        return abs_path;
    }
    return safe_strdup(path);
}

int path_matches_delete(const char* stored, const char* key) {
    char stored_copy[PATH_MAX];
    if (strlen(stored) >= sizeof(stored_copy)) {
        return 0;
    }
    strcpy(stored_copy, stored);
    stored_copy[strcspn(stored_copy, "\n")] = '\0';
    return strcmp(stored_copy, key) == 0;
}

int key_in_list(char** keys, size_t count, const char* key) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(keys[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

int keys_add(char*** keys, size_t* count, const char* path) {
    char* key = resolve_delete_key(path);
    if (!key) {
        return -1;
    }
    if (key_in_list(*keys, *count, key)) {
        free(key);
        return 0;
    }
    char** tmp = realloc(*keys, (*count + 1) * sizeof(char*));
    if (!tmp) {
        perror("Failed to allocate memory");
        free(key);
        return -1;
    }
    *keys = tmp;
    (*keys)[*count] = key;
    (*count)++;
    return 0;
}

void keys_free(char** keys, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

int collect_delete_targets(int argc, char** argv, char*** keys_out, size_t* count_out) {
    char** keys = NULL;
    size_t count = 0;

    for (int i = 0; i < argc; i++) {
        glob_t glob_result;
        int glob_ok = glob(argv[i], GLOB_TILDE | GLOB_MARK, NULL, &glob_result);
        if (glob_ok == 0 && glob_result.gl_pathc > 0) {
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                if (keys_add(&keys, &count, glob_result.gl_pathv[j]) != 0) {
                    keys_free(keys, count);
                    globfree(&glob_result);
                    return -1;
                }
            }
            globfree(&glob_result);
        } else {
            if (glob_ok == 0) {
                globfree(&glob_result);
            }
            if (keys_add(&keys, &count, argv[i]) != 0) {
                keys_free(keys, count);
                return -1;
            }
        }
    }

    if (!isatty(fileno(stdin))) {
        char* line = NULL;
        size_t len = 0;
        while (getline(&line, &len, stdin) != -1) {
            line[strcspn(line, "\n")] = '\0';
            if (line[0] != '\0' && keys_add(&keys, &count, line) != 0) {
                free(line);
                keys_free(keys, count);
                return -1;
            }
        }
        free(line);
    }

    *keys_out = keys;
    *count_out = count;
    return 0;
}

int line_matches_keys(const char* line, char** keys, size_t key_count) {
    for (size_t i = 0; i < key_count; i++) {
        if (path_matches_delete(line, keys[i])) {
            return 1;
        }
    }
    return 0;
}

int tombstone_line_at(FILE* temp_file, off_t offset, size_t line_len) {
    char* buf = malloc(line_len);
    if (!buf) {
        perror("Failed to allocate memory");
        return -1;
    }
    memset(buf, ' ', line_len);
    buf[line_len - 1] = '\n';
    if (fseek(temp_file, offset, SEEK_SET) != 0) {
        perror("Failed to seek temp file");
        free(buf);
        return -1;
    }
    if (fwrite(buf, 1, line_len, temp_file) != line_len) {
        perror("Failed to tombstone line");
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int tombstone_hash_at(FILE* index_file, int line_index) {
    unsigned char zero_hash[HASH_SIZE];
    memset(zero_hash, 0, HASH_SIZE);
    if (fseek(index_file, (long)line_index * HASH_SIZE, SEEK_SET) != 0) {
        perror("Failed to seek index file");
        return -1;
    }
    if (fwrite(zero_hash, HASH_SIZE, 1, index_file) != 1) {
        perror("Failed to tombstone hash");
        return -1;
    }
    return 0;
}

int delete_mode(int argc, char** argv, int flags) {
    if (lock_file_exists() && !(flags & FORCE_FLAG)) {
        fprintf(stderr, "Error: Lock file exists\n");
        return -1;
    }
    if (create_lock_file(flags & FORCE_FLAG) != 0) {
        return -1;
    }

    char** keys = NULL;
    size_t key_count = 0;
    if (collect_delete_targets(argc, argv, &keys, &key_count) != 0) {
        remove_lock_file();
        return -1;
    }

    int removed = 0;
    if (access(temp_filename, F_OK) != -1) {
        FILE* temp_file = fopen(temp_filename, "r+");
        if (!temp_file) {
            perror("Failed to open temp file");
            keys_free(keys, key_count);
            remove_lock_file();
            return -1;
        }
        FILE* index_file = fopen(index_filename, "r+");
        if (!index_file) {
            perror("Failed to open index file");
            fclose(temp_file);
            keys_free(keys, key_count);
            remove_lock_file();
            return -1;
        }

        char* line = NULL;
        size_t len = 0;
        ssize_t read;
        off_t offset = 0;
        int line_index = 0;

        while ((read = getline(&line, &len, temp_file)) != -1) {
            size_t line_len = (size_t)read;
            if (is_active_line(line) && line_matches_keys(line, keys, key_count)) {
                if (tombstone_line_at(temp_file, offset, line_len) != 0 ||
                    tombstone_hash_at(index_file, line_index) != 0) {
                    free(line);
                    fclose(temp_file);
                    fclose(index_file);
                    keys_free(keys, key_count);
                    remove_lock_file();
                    return -1;
                }
                removed++;
            }
            offset += read;
            line_index++;
        }
        free(line);
        fclose(temp_file);
        fclose(index_file);
    }

    keys_free(keys, key_count);

    if (maybe_compact() != 0) {
        remove_lock_file();
        return -1;
    }

    if (!(flags & QUIET_FLAG)) {
        int active = 0;
        int tombstones = 0;
        count_storage(&active, &tombstones);
        (void)tombstones;
        printf("%d paths removed / %d paths total\n", removed, active);
    }

    remove_lock_file();
    return 0;
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
           "  -l          Long format output (like ls -l)\n"
           "  -d          Remove paths from selection\n"
           "  -h          Show this help\n"
           "\n"
           "When no paths are provided, list mode is used by default.\n"
           "When paths are provided without -r, they are added to the selection.\n");
    return 0;
}

int main(int argc, char** argv) {
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }

    int uid = getuid();
    int ret;
    ret = snprintf(temp_filename, sizeof(temp_filename), "%s/fsel_%d.tmp", tmpdir, uid);
    if (ret < 0 || ret >= (int)sizeof(temp_filename)) {
        fprintf(stderr, "Error: Path too long for temp file\n");
        return EXIT_FAILURE;
    }
    ret = snprintf(index_filename, sizeof(index_filename), "%s/fsel_%d.idx", tmpdir, uid);
    if (ret < 0 || ret >= (int)sizeof(index_filename)) {
        fprintf(stderr, "Error: Path too long for index file\n");
        return EXIT_FAILURE;
    }
    ret = snprintf(lock_filename, sizeof(lock_filename), "%s/fsel_%d.lock", tmpdir, uid);
    if (ret < 0 || ret >= (int)sizeof(lock_filename)) {
        fprintf(stderr, "Error: Path too long for lock file\n");
        return EXIT_FAILURE;
    }

    int opt;
    int flags = 0;
    while ((opt = getopt(argc, argv, "qscfruvhld")) != -1) {
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
            case 'l':
                flags |= LONG_FORMAT_FLAG;
                break;
            case 'd':
                flags |= DELETE_FLAG;
                break;
            case 'h':
                return print_help();
            default:
                fprintf(stderr, "Invalid option\n");
                return EXIT_FAILURE;
        }
    }

    if (flags & UNLOCK_FLAG) {
        return unlock_mode(0, NULL, flags);
    }

    if (flags & VALIDATE_FLAG) {
        return validate_mode(0, NULL, flags);
    }

    if (flags & CLEAR_FLAG && optind >= argc) {
        return clear_mode(0, NULL, flags);
    }

    if (flags & DELETE_FLAG) {
        if (flags & REPLACE_FLAG) {
            fprintf(stderr, "Error: incompatible options -d and -r\n");
            return EXIT_FAILURE;
        }
        int has_input = !isatty(fileno(stdin));
        if (optind >= argc && !has_input) {
            fprintf(stderr, "Error: delete requires paths\n");
            return EXIT_FAILURE;
        }
        return delete_mode(argc - optind, argv + optind, flags);
    }

    int has_input = !isatty(fileno(stdin));
    if (optind >= argc && !has_input) {
        // When no paths are provided, default to list mode
        return list_mode(0, NULL, flags);
    }

    // When paths are provided, use add mode
    return add_mode(argc - optind, argv + optind, flags);
}
