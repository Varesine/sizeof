#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "cmd_ls.h"
#include "scanner.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

typedef struct {
    char *name;
    char *full_path;
    uint64_t apparent_size;
    uint64_t disk_size;
    uint64_t file_count;
    uint64_t dir_count;
    bool is_directory;
    bool is_symlink;
    bool exists;
} LsItem;

static bool g_sort_disk = false;
static bool g_sort_reverse = false;

static int compare_ls_items(const void *a, const void *b) {
    const LsItem *ia = (const LsItem *)a;
    const LsItem *ib = (const LsItem *)b;

    uint64_t sa = g_sort_disk ? ia->disk_size : ia->apparent_size;
    uint64_t sb = g_sort_disk ? ib->disk_size : ib->apparent_size;

    if (sa != sb) {
        if (g_sort_reverse) {
            return (sa < sb) ? -1 : 1;
        } else {
            return (sa > sb) ? -1 : 1;
        }
    }

    return strcmp(ia->name, ib->name);
}

static void print_ls_help(void) {
    printf("Usage: sizeof ls [OPTIONS] [PATH]\n\n");
    printf("List files and folders in PATH sorted by size (largest first).\n");
    printf("When directories are encountered, their total size is computed concurrently.\n\n");
    printf("Options:\n");
    printf("  -top NUM, -n NUM       Show only top NUM largest files and folders (e.g. -top 3)\n");
    printf("  -d, --disk             Sort and display filesystem allocated disk size (blocks * 512)\n");
    printf("  -a, --all              Include hidden files and folders [default includes hidden]\n");
    printf("      --no-hidden        Exclude hidden files and folders (dotfiles)\n");
    printf("  -r, --reverse          Reverse sort order (smallest first)\n");
    printf("  -b, --bytes            Print sizes in raw bytes only\n");
    printf("      --si               Use decimal 1000-based units (kB, MB, GB) instead of 1024\n");
    printf("      --json             Output results in JSON format\n");
    printf("      --no-color         Disable terminal colors\n");
    printf("  -h, --help             Display this help message and exit\n\n");
    printf("Examples:\n");
    printf("  sizeof ls\n");
    printf("  sizeof ls -top 3\n");
    printf("  sizeof ls -top 5 /var/log\n");
    printf("  sizeof ls -r -top 10\n");
}

int cmd_ls(int argc, char *argv[]) {
    int top_n = 0;
    bool include_hidden = true;
    bool disk_usage = false;
    bool reverse = false;
    bool raw_bytes = false;
    bool json_output = false;
    bool si_units = false;
    bool color = isatty(STDOUT_FILENO) && (getenv("NO_COLOR") == NULL);

    static struct option long_options[] = {
        {"top",         required_argument, 0, 'T'},
        {"all",         no_argument,       0, 'a'},
        {"no-hidden",   no_argument,       0, 1000},
        {"disk",        no_argument,       0, 'd'},
        {"apparent",    no_argument,       0, 1001},
        {"reverse",     no_argument,       0, 'r'},
        {"bytes",       no_argument,       0, 'b'},
        {"json",        no_argument,       0, 1002},
        {"si",          no_argument,       0, 1003},
        {"no-color",    no_argument,       0, 1004},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Reset optind for subcommands */
    optind = 1;
    int opt;
    while ((opt = getopt_long_only(argc, argv, "T:n:abdrh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'T':
            case 'n': {
                top_n = atoi(optarg);
                if (top_n <= 0) {
                    fprintf(stderr, "sizeof ls: error: invalid top count '%s'\n", optarg);
                    return 1;
                }
                break;
            }
            case 'a':
                include_hidden = true;
                break;
            case 1000:
                include_hidden = false;
                break;
            case 'd':
                disk_usage = true;
                break;
            case 1001:
                disk_usage = false;
                break;
            case 'r':
                reverse = true;
                break;
            case 'b':
                raw_bytes = true;
                break;
            case 1002:
                json_output = true;
                break;
            case 1003:
                si_units = true;
                break;
            case 1004:
                color = false;
                break;
            case 'h':
                print_ls_help();
                return 0;
            default:
                fprintf(stderr, "Try 'sizeof ls --help' for more information.\n");
                return 1;
        }
    }

    const char *target_dir = (optind < argc) ? argv[optind] : ".";

    struct stat target_st;
    if (lstat(target_dir, &target_st) != 0) {
        fprintf(stderr, "sizeof ls: cannot access '%s': %s\n", target_dir, strerror(errno));
        return 1;
    }

    /* If target is a single file, output it directly */
    if (!S_ISDIR(target_st.st_mode)) {
        char size_buf[32];
        uint64_t sz = disk_usage ? (uint64_t)target_st.st_blocks * 512 : (uint64_t)target_st.st_size;
        format_bytes(sz, size_buf, sizeof(size_buf), si_units);
        printf("%10s  FILE  %s\n", size_buf, target_dir);
        return 0;
    }

    DIR *dir = opendir(target_dir);
    if (!dir) {
        fprintf(stderr, "sizeof ls: cannot open directory '%s': %s\n", target_dir, strerror(errno));
        return 1;
    }

    /* Collect directory entries */
    size_t items_capacity = 32;
    size_t items_count = 0;
    LsItem *items = malloc(items_capacity * sizeof(LsItem));
    if (!items) {
        closedir(dir);
        return 1;
    }

    size_t target_len = strlen(target_dir);
    bool need_slash = (target_len > 0 && target_dir[target_len - 1] != '/');

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (!include_hidden && name[0] == '.') {
            continue;
        }

        size_t name_len = strlen(name);
        size_t full_len = target_len + (need_slash ? 1 : 0) + name_len + 1;
        char *full_path = malloc(full_len);
        if (!full_path) continue;
        snprintf(full_path, full_len, "%s%s%s", target_dir, need_slash ? "/" : "", name);

        if (items_count >= items_capacity) {
            items_capacity *= 2;
            LsItem *new_items = realloc(items, items_capacity * sizeof(LsItem));
            if (!new_items) {
                free(full_path);
                break;
            }
            items = new_items;
        }

        items[items_count].name = strdup(name);
        items[items_count].full_path = full_path;
        items[items_count].apparent_size = 0;
        items[items_count].disk_size = 0;
        items[items_count].file_count = 0;
        items[items_count].dir_count = 0;
        items[items_count].is_directory = false;
        items[items_count].is_symlink = false;
        items[items_count].exists = false;
        items_count++;
    }

    closedir(dir);

    if (items_count == 0) {
        printf("Directory '%s' is empty.\n", target_dir);
        free(items);
        return 0;
    }

    /* Scan each item and compute total sizes (directories use multithreaded scanning) */
    ScanOptions scan_opts;
    memset(&scan_opts, 0, sizeof(scan_opts));
    scan_opts.num_threads = 0; /* Auto-detect hardware threads */
    scan_opts.one_file_system = false;
    scan_opts.verbose = false;
    scan_opts.progress = NULL; /* No nested sub-progress bar to keep output crisp */

    uint64_t total_dir_apparent = 0;
    uint64_t total_dir_disk = 0;

    for (size_t i = 0; i < items_count; i++) {
        ScanResult res;
        if (scan_path(items[i].full_path, &scan_opts, &res)) {
            items[i].exists = true;
            items[i].is_directory = res.is_directory;
            items[i].apparent_size = res.apparent_size;
            items[i].disk_size = res.disk_size;
            items[i].file_count = res.file_count;
            items[i].dir_count = res.dir_count;
            items[i].is_symlink = (res.symlink_count > 0 && !res.is_directory);

            total_dir_apparent += res.apparent_size;
            total_dir_disk     += res.disk_size;
        }
    }

    /* Sort items by size */
    g_sort_disk = disk_usage;
    g_sort_reverse = reverse;
    qsort(items, items_count, sizeof(LsItem), compare_ls_items);

    /* Determine number of items to display */
    size_t display_count = items_count;
    if (top_n > 0 && (size_t)top_n < items_count) {
        display_count = (size_t)top_n;
    }

    /* JSON Output format */
    if (json_output) {
        printf("[\n");
        for (size_t i = 0; i < display_count; i++) {
            printf("  {\n");
            printf("    \"name\": \"%s\",\n", items[i].name);
            printf("    \"path\": \"%s\",\n", items[i].full_path);
            printf("    \"type\": \"%s\",\n", items[i].is_directory ? "directory" : (items[i].is_symlink ? "symlink" : "file"));
            printf("    \"apparent_size_bytes\": %llu,\n", (unsigned long long)items[i].apparent_size);
            printf("    \"disk_size_bytes\": %llu,\n", (unsigned long long)items[i].disk_size);
            printf("    \"file_count\": %llu,\n", (unsigned long long)items[i].file_count);
            printf("    \"dir_count\": %llu\n", (unsigned long long)items[i].dir_count);
            printf("  }%s\n", (i + 1 < display_count) ? "," : "");
        }
        printf("]\n");

        for (size_t i = 0; i < items_count; i++) {
            free(items[i].name);
            free(items[i].full_path);
        }
        free(items);
        return 0;
    }

    /* Raw bytes format */
    if (raw_bytes) {
        for (size_t i = 0; i < display_count; i++) {
            uint64_t val = disk_usage ? items[i].disk_size : items[i].apparent_size;
            printf("%llu\t%s%s\n", (unsigned long long)val, items[i].name, items[i].is_directory ? "/" : "");
        }

        for (size_t i = 0; i < items_count; i++) {
            free(items[i].name);
            free(items[i].full_path);
        }
        free(items);
        return 0;
    }

    /* Table Output format */
    const char *c_bold   = color ? "\033[1m" : "";
    const char *c_green  = color ? "\033[32m" : "";
    const char *c_cyan   = color ? "\033[36m" : "";
    const char *c_blue   = color ? "\033[34m" : "";
    const char *c_yellow = color ? "\033[33m" : "";
    const char *c_dim    = color ? "\033[2m" : "";
    const char *c_reset  = color ? "\033[0m" : "";

    printf("%s   %-10s  %-4s  %-14s  %-s%s\n",
           c_bold, "SIZE", "TYPE", "ITEMS", "NAME", c_reset);
    printf("%s──────────────────────────────────────────────────────────────────%s\n",
           c_dim, c_reset);

    uint64_t displayed_total = 0;
    for (size_t i = 0; i < display_count; i++) {
        uint64_t sz = disk_usage ? items[i].disk_size : items[i].apparent_size;
        displayed_total += sz;

        char size_buf[32];
        format_bytes(sz, size_buf, sizeof(size_buf), si_units);

        char items_buf[32];
        if (items[i].is_directory) {
            char cnt_str[32];
            format_number(items[i].file_count, cnt_str, sizeof(cnt_str));
            snprintf(items_buf, sizeof(items_buf), "%s files", cnt_str);
        } else {
            snprintf(items_buf, sizeof(items_buf), "-");
        }

        const char *type_str = items[i].is_directory ? "DIR " : (items[i].is_symlink ? "LINK" : "FILE");
        const char *type_col = items[i].is_directory ? c_blue : (items[i].is_symlink ? c_yellow : c_dim);
        const char *name_col = items[i].is_directory ? c_cyan : "";
        const char *suffix   = items[i].is_directory ? "/" : "";

        printf("%s%10s%s  %s%s%s  %-14s  %s%s%s%s%s\n",
               c_green, size_buf, c_reset,
               type_col, type_str, c_reset,
               items_buf,
               c_bold, name_col, items[i].name, suffix, c_reset);
    }

    printf("%s──────────────────────────────────────────────────────────────────%s\n",
           c_dim, c_reset);

    uint64_t total_val = disk_usage ? total_dir_disk : total_dir_apparent;
    char total_buf[32];
    format_bytes(total_val, total_buf, sizeof(total_buf), si_units);

    if (display_count < items_count) {
        char disp_buf[32];
        format_bytes(displayed_total, disp_buf, sizeof(disp_buf), si_units);
        printf("%sTop %zu of %zu items:%s %s%s%s %s(Total folder: %s)%s\n",
               c_bold, display_count, items_count, c_reset,
               c_green, disp_buf, c_reset,
               c_dim, total_buf, c_reset);
    } else {
        char total_bytes_str[32];
        format_number(total_val, total_bytes_str, sizeof(total_bytes_str));
        printf("%sTotal:%s %s%s%s (%s bytes) across %zu items\n",
               c_bold, c_reset,
               c_green, total_buf, c_reset,
               total_bytes_str, items_count);
    }

    /* Cleanup */
    for (size_t i = 0; i < items_count; i++) {
        free(items[i].name);
        free(items[i].full_path);
    }
    free(items);

    return 0;
}
