#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

#define INITIAL_FILE_CAPACITY 1024
#define MAX_FILE_CAPACITY 100000
#define INITIAL_USES_CAPACITY 16
#define MAX_USES_CAPACITY 10000
#define MAX_LINE 1024
#define MAX_MODULE_LEN 100
#define HASH_SIZE 16384

typedef struct FortranFile {
    char filename[1024];
    char module_name[MAX_MODULE_LEN];  // lowercase, null-terminated
    int *uses;       // store indices of modules used (indices in files[])
    int uses_count;
    int uses_capacity;
} FortranFile;

FortranFile *files = NULL;
int file_count = 0;
int file_capacity = 0;

typedef struct HashEntry {
    char key[MAX_MODULE_LEN];
    int value;
    struct HashEntry *next;
} HashEntry;

HashEntry *hash_table[HASH_SIZE] = {0};

unsigned int fnv1a_hash(const char *str) {
    const unsigned int FNV_prime = 16777619U;
    unsigned int hash = 2166136261U;
    while (*str) {
        hash ^= (unsigned char)(*str++);
        hash *= FNV_prime;
    }
    return hash % HASH_SIZE;
}

unsigned int hash_func(const char *str) {
    return fnv1a_hash(str);
}

void hash_insert(const char *key, int value) {
    unsigned int h = hash_func(key);
    HashEntry *entry = malloc(sizeof(HashEntry));
    if (!entry) {
        fprintf(stderr, "malloc failed in hash_insert\n");
        exit(1);
    }
    strcpy(entry->key, key);
    entry->value = value;
    entry->next = hash_table[h];
    hash_table[h] = entry;
}

int hash_lookup(const char *key) {
    unsigned int h = hash_func(key);
    for (HashEntry *e = hash_table[h]; e != NULL; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->value;
    }
    return -1;
}

void free_hash_table() {
    for (int i = 0; i < HASH_SIZE; i++) {
        HashEntry *e = hash_table[i];
        while (e) {
            HashEntry *next = e->next;
            free(e);
            e = next;
        }
        hash_table[i] = NULL;
    }
}

void str_tolower(char *s) {
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = 0;
    return str;
}

int iscomma(const char item) {
    return ((int)item == 44);
}

//Extracts the second work beyond "use". Note, it is common in fortran
//for the module to be used like use module_name, only: so we need to break on the comma
//as well.
int extract_second_word(const char *line, char *dst, int max_len) {
    int i = 0;
    while (line[i] && !isspace((unsigned char)line[i])) i++; // skip first word
    while (line[i] && isspace((unsigned char)line[i])) i++;
    int j = 0;
    while (line[i] && !isspace((unsigned char)line[i]) && !iscomma((unsigned char)line[i]) && j < max_len - 1) {
        dst[j++] = (char)tolower((unsigned char)line[i++]);
    }
    dst[j] = 0;
    return (j > 0);
}

void ensure_file_capacity() {
    if (file_count >= file_capacity) {
        int new_capacity = file_capacity == 0 ? INITIAL_FILE_CAPACITY : file_capacity * 2;
        assert(new_capacity <= MAX_FILE_CAPACITY && "Exceeded max number of files");
        FortranFile *new_files = realloc(files, new_capacity * sizeof(FortranFile));
        if (!new_files) {
            fprintf(stderr, "realloc failed for files\n");
            exit(1);
        }
        // Initialize new entries
        for (int i = file_capacity; i < new_capacity; i++) {
            new_files[i].uses = NULL;
            new_files[i].uses_capacity = 0;
            new_files[i].uses_count = 0;
            new_files[i].module_name[0] = 0;
            new_files[i].filename[0] = 0;
        }
        files = new_files;
        file_capacity = new_capacity;
    }
}

void ensure_uses_capacity(int idx) {
    FortranFile *f = &files[idx];
    if (f->uses_count >= f->uses_capacity) {
        int new_capacity = f->uses_capacity == 0 ? INITIAL_USES_CAPACITY : f->uses_capacity * 2;
        assert(new_capacity <= MAX_USES_CAPACITY && "Exceeded max uses per file");
        int *new_uses = realloc(f->uses, new_capacity * sizeof(int));
        if (!new_uses) {
            fprintf(stderr, "realloc failed for uses\n");
            exit(1);
        }
        f->uses = new_uses;
        f->uses_capacity = new_capacity;
    }
}

void add_used_module(int file_idx, int used_idx) {
    FortranFile *f = &files[file_idx];
    for (int i = 0; i < f->uses_count; i++) {
        if (f->uses[i] == used_idx) return;
    }
    ensure_uses_capacity(file_idx);
    f->uses[f->uses_count++] = used_idx;
}

int strcmp_case_insensitive(char const *a, char const *b)
{
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
}

void read_files_in_dir(const char *dir_path, int recursive) {
    DIR *d = opendir(dir_path);
    if (!d) {
        perror(dir_path);
        exit(1);
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        // Skip . and .. characters for sub directories.
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        // Construct full path
        char path[512];
        int ret = snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);
        if (ret < 0 || ret >= (int)sizeof(path)) {
            fprintf(stderr, "Directory Path too long: %s/%s\n", dir_path, de->d_name);
            closedir(d);
            exit(1);
        }

        struct stat st;
        if (stat(path, &st) == -1) {
            perror(path);
            closedir(d);
            exit(1);
        }

        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                read_files_in_dir(path, recursive);
            }
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(de->d_name);
            if ((len >= 4 && strcmp_case_insensitive(de->d_name + len - 4, ".f90") == 0) ||
                (len >= 4 && strcmp_case_insensitive(de->d_name + len - 4, ".for") == 0)) {
                ensure_file_capacity();
                strncpy(files[file_count].filename, path, sizeof(files[file_count].filename)-1);
                files[file_count].filename[sizeof(files[file_count].filename)-1] = 0;
                files[file_count].module_name[0] = 0;
                files[file_count].uses = NULL;
                files[file_count].uses_capacity = 0;
                files[file_count].uses_count = 0;
                file_count++;
            }
        }
    }
    closedir(d);
}

char *strcasestr_custom(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n)
            return (char *)haystack;
    }
    return NULL;
}

void find_defined_modules() {
    char line[MAX_LINE], modname[MAX_MODULE_LEN];
    for (int i = 0; i < file_count; i++) {
        FILE *fp = fopen(files[i].filename, "r");
        if (!fp) {
            perror(files[i].filename);
            exit(1);
        }
        while (fgets(line, sizeof(line), fp)) {
            char *ptr = trim(line);
            if (strncasecmp(ptr, "module ", 7) == 0) {
                if (strcasestr_custom(ptr, "procedure") == NULL) {
                    if (extract_second_word(ptr, modname, MAX_MODULE_LEN)) {
                        strcpy(files[i].module_name, modname);
                        hash_insert(modname, i);
                        //break;  // stop after first module per file
                    }
                }
            }
        }
        fclose(fp);
    }
}

void find_used_modules() {
    char line[MAX_LINE], usedmod[MAX_MODULE_LEN];
    for (int i = 0; i < file_count; i++) {
        FILE *fp = fopen(files[i].filename, "r");
        if (!fp) {
            perror(files[i].filename);
            exit(1);
        }
        while (fgets(line, sizeof(line), fp)) {
            char *ptr = trim(line);
            if (strncasecmp(ptr, "use ", 4) == 0) {
                if (extract_second_word(ptr, usedmod, MAX_MODULE_LEN)) {
                    int idx = hash_lookup(usedmod);
                    if (idx != -1) {
                        add_used_module(i, idx);
                    }
                }
            }
        }
        fclose(fp);
    }
}

typedef struct {
    int *edges;
    int count;
    int capacity;
} AdjList;

AdjList *adj = NULL;
int *in_degree = NULL;

void ensure_adj_capacity(int u) {
    if (adj[u].count >= adj[u].capacity) {
        int new_capacity = adj[u].capacity == 0 ? 4 : adj[u].capacity * 2;
        int *new_edges = realloc(adj[u].edges, new_capacity * sizeof(int));
        if (!new_edges) {
            fprintf(stderr, "realloc failed for adjacency list\n");
            exit(1);
        }
        adj[u].edges = new_edges;
        adj[u].capacity = new_capacity;
    }
}

void build_graph() {
    adj = calloc(file_count, sizeof(AdjList));
    in_degree = calloc(file_count, sizeof(int));
    if (!adj || !in_degree) {
        fprintf(stderr, "calloc failed for graph\n");
        exit(1);
    }
    for (int i = 0; i < file_count; i++) {
        adj[i].edges = NULL;
        adj[i].capacity = 0;
        adj[i].count = 0;
        in_degree[i] = 0;
    }
    for (int i = 0; i < file_count; i++) {
        FortranFile *f = &files[i];
        for (int j = 0; j < f->uses_count; j++) {
            int dep = f->uses[j];
            ensure_adj_capacity(dep);
            adj[dep].edges[adj[dep].count++] = i;
            in_degree[i]++;
        }
    }
}

//Topological sort of files based on module dependencies
//  Returns 1 if we could sort
//  Returns 0 if we detected a cycle. 
int topologic_sort(int *sorted, int *sorted_len) {
    int *queue = malloc(file_count * sizeof(int));
    if (!queue) {
        fprintf(stderr, "malloc failed for topo queue\n");
        exit(1);
    }
    int front = 0, back = 0;
    for (int i = 0; i < file_count; i++) {
        if (in_degree[i] == 0) queue[back++] = i;
    }
    int count = 0;
    while (front < back) {
        int u = queue[front++];
        sorted[count++] = u;
        for (int i = 0; i < adj[u].count; i++) {
            int v = adj[u].edges[i];
            in_degree[v]--;
            if (in_degree[v] == 0) queue[back++] = v;
        }
    }
    free(queue);
    if (count != file_count) {
        return 0;  // cycle detected
    }
    *sorted_len = count;
    return 1;
}

/**
 * split_dirs - splits comma separated list of directories into array
 * list: string containing comma-separated directory list
 * count: pointer to store number of directories parsed
 *
 * Returns dynamically allocated array of strings (each dynamically allocated)
 * Caller must free strings and array.
 */
char **split_dirs(char *list, int *count) {
    int capacity = 8;
    char **dirs = malloc(capacity * sizeof(char *));
    if (!dirs) {
        fprintf(stderr, "malloc failed in split_dirs\n");
        exit(1);
    }
    *count = 0;

    char *token = strtok(list, ",");
    while (token) {
        char *dir = trim(token);
        if (*dir != 0) {
            if (*count >= capacity) {
                capacity *= 2;
                char **new_dirs = realloc(dirs, capacity * sizeof(char *));
                if (!new_dirs) {
                    fprintf(stderr, "realloc failed in split_dirs\n");
                    exit(1);
                }
                dirs = new_dirs;
            }
            dirs[*count] = strdup(dir);
            if (!dirs[*count]) {
                fprintf(stderr, "strdup failed in split_dirs\n");
                exit(1);
            }
            (*count)++;
        }
        token = strtok(NULL, ",");
    }
    return dirs;
}

/**
 * free_dirs - free array of directory strings returned by split_dirs
 */
void free_dirs(char **dirs, int count) {
    if (!dirs) return;
    for (int i = 0; i < count; i++) free(dirs[i]);
    free(dirs);
}

/**
 * print_help - prints usage information
 */
void print_help(const char *progname) {
    printf(
        "Usage: %s [-d dirs] [-D dirs] [-m] [-h]\n"
        "\n"
        "Scans Fortran .f90 source files to determine module dependencies,\n"
        "then outputs the topologic build order of modules.\n"
        "\n"
        "Flags:\n"
        "  -d DIRS    Comma-separated list of directories to scan non-recursively.\n"
        "             Only one -d flag allowed.\n"
        "  -D DIRS    Comma-separated list of directories to scan recursively.\n"
        "             Only one -D flag allowed.\n"
        "  -m         Print a Makefile dependency list instead of build order.\n"
        "  -h         Show this help message.\n"
        "\n"
        "If neither -d nor -D is specified, defaults to scanning 'src' non-recursively.\n"
    , progname);
}

int main(int argc, char **argv) {
    /*
    Program: Fortran module dependency analyzer and topologic build order printer.

    Usage:
      -d DIRS   Comma-separated list of directories to scan non-recursively.
                Only one -d flag allowed.
      -D DIRS   Comma-separated list of directories to scan recursively.
                Only one -D flag allowed.
      -m        Print a Makefile dependency list instead of build order.
      -h        Show this help message.

    Description:
      Scans .f90 files in given directories to detect Fortran modules and their
      'use' dependencies, then computes a topologic order for building modules.
      Can output either the ordered list of files to build or a Makefile
      dependency list suitable for build systems.

    Notes:
      - Repeated use of -d or -D flags is an error.
      - Directories in the flags are comma-separated and will be scanned in the order
        given.
      - If neither -d nor -D is specified, defaults to scanning 'src' non-recursively.

    Author:
        Drake Gates
    */

    //Allocate the direcotry pointers.
    char *d_dirs_str = NULL;
    char *D_dirs_str = NULL;
    int print_make_deps = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            if (d_dirs_str != NULL) {
                fprintf(stderr, "Error: -d flag specified more than once\n");
                return 1;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -d flag requires an argument\n");
                return 1;
            }
            d_dirs_str = argv[++i];
        } else if (strcmp(argv[i], "-D") == 0) {
            if (D_dirs_str != NULL) {
                fprintf(stderr, "Error: -D flag specified more than once\n");
                return 1;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -D flag requires an argument\n");
                return 1;
            }
            D_dirs_str = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0) {
            print_make_deps = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    int d_count = 0, D_count = 0;
    char **d_dirs = NULL, **D_dirs = NULL;

    if (d_dirs_str) {
        d_dirs = split_dirs(d_dirs_str, &d_count);
        if (d_count == 0) {
            fprintf(stderr, "Error: -d flag requires at least one directory\n");
            return 1;
        }
    }

    if (D_dirs_str) {
        D_dirs = split_dirs(D_dirs_str, &D_count);
        if (D_count == 0) {
            fprintf(stderr, "Error: -D flag requires at least one directory\n");
            return 1;
        }
    }

    if (!d_dirs && !D_dirs) {
        // Default to "src" non-recursive
        d_count = 1;
        d_dirs = malloc(sizeof(char *));
        if (!d_dirs) {
            fprintf(stderr, "malloc failed for default dir\n");
            return 1;
        }
        d_dirs[0] = strdup("src");
        if (!d_dirs[0]) {
            fprintf(stderr, "strdup failed for default dir\n");
            return 1;
        }
    }

    // Read all files in all directories
    for (int i = 0; i < d_count; i++) {
        read_files_in_dir(d_dirs[i], 0);
    }
    for (int i = 0; i < D_count; i++) {
        read_files_in_dir(D_dirs[i], 1);
    }

    free_dirs(d_dirs, d_count);
    free_dirs(D_dirs, D_count);

    if (file_count == 0) {
        fprintf(stderr, "No .f90 files found to process.\n");
        return 1;
    }

    find_defined_modules();
    find_used_modules();

    build_graph();

    int *sorted = malloc(file_count * sizeof(int));
    if (!sorted) {
        fprintf(stderr, "malloc failed for sorted\n");
        return 1;
    }

    if (!topologic_sort(sorted, &file_count)) {
        fprintf(stderr, "Error: cyclic dependency detected, no valid build order\n");
        free(sorted);
        free_hash_table();
        for (int i = 0; i < file_count; i++) free(files[i].uses);
        free(files);
        free(adj);
        free(in_degree);
        return 1;
    }

    if (print_make_deps) {
        // Print Makefile dependency list: filename: dependencies filenames...
        for (int i = 0; i < file_count; i++) {
            int idx = sorted[i];
            printf("%s:", files[idx].filename);
            FortranFile *f = &files[idx];
            for (int u = 0; u < f->uses_count; u++) {
                int dep_idx = f->uses[u];
                printf(" %s", files[dep_idx].filename);
            }
            printf("\n");
        }
    } else {
        // Print build order (filenames only)
        for (int i = 0; i < file_count; i++) {
            printf("%s\n", files[sorted[i]].filename);
        }
    }

    free(sorted);
    free_hash_table();
    for (int i = 0; i < file_count; i++) free(files[i].uses);
    free(files);
    free(adj);
    free(in_degree);

    return 0;
}