#define _POSIX_C_SOURCE 200809L
#include "scanner.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>

#define VERSION "1.0.0"

typedef enum {
    OUTPUT_DEFAULT,
    OUTPUT_BYTES,
    OUTPUT_KB,
    OUTPUT_MB,
    OUTPUT_GB,
    OUTPUT_SHORT,
    OUTPUT_JSON,
    OUTPUT_VERBOSE
} OutputFormat;

typedef struct {
    OutputFormat format;
    bool disk_usage;       /* Use disk allocated blocks instead of apparent size */
    bool si_units;         /* 1000-based vs 1024-based */
    bool color;            /* Terminal colors enabled */
    ScanOptions scan_opts;
} Config;

static void print_version(void) {
    printf("sizeof %s (multithreaded folder/file size calculator)\n", VERSION);
}

static void print_help(const char *prog_name) {
    printf("Usage: %s [OPTIONS] [PATH...]\n\n", prog_name);
    printf("Calculate and display the total size of files or directories.\n");
    printf("When given a folder, multithreading is used for high performance.\n\n");
    printf("Arguments:\n");
    printf("  PATH                   File or directory to inspect (defaults to current directory '.')\n\n");
    printf("Options:\n");
    printf("  -b, --bytes            Print size as raw bytes only\n");
    printf("  -k, --kb               Print size in Kilobytes (KiB)\n");
    printf("  -m, --mb               Print size in Megabytes (MiB)\n");
    printf("  -g, --gb               Print size in Gigabytes (GiB)\n");
    printf("  -s, --short            Print only human-readable size string (e.g. '1.45 GB')\n");
    printf("  -d, --disk             Show disk space allocated (blocks * 512) instead of apparent size\n");
    printf("  -a, --apparent         Show apparent file content size [default]\n");
    printf("  -v, --verbose          Display detailed breakdown (files, dirs, time, disk vs apparent)\n");
    printf("  -t, --threads NUM      Set number of worker threads (default: hardware CPU count)\n");
    printf("  -x, --one-file-system  Do not cross filesystem mount boundaries\n");
    printf("      --si               Use decimal 1000-based units (kB, MB, GB) instead of 1024\n");
    printf("      --no-color         Disable colored output\n");
    printf("      --json             Output results as JSON\n");
    printf("      --help             Display this help message and exit\n");
    printf("      --version          Output version information and exit\n\n");
    printf("Examples:\n");
    printf("  sizeof some.file\n");
    printf("  sizeof /some/folder\n");
    printf("  sizeof -v ~/Documents\n");
    printf("  sizeof -b large_file.iso\n");
    printf("  sizeof file1.txt dir1 file2.bin\n");
}

static void print_result(const char *path, const ScanResult *res, const Config *cfg, bool show_path) {
    uint64_t size_val = cfg->disk_usage ? res->disk_size : res->apparent_size;

    const char *col_bold  = cfg->color ? "\033[1m" : "";
    const char *col_green = cfg->color ? "\033[32m" : "";
    const char *col_cyan  = cfg->color ? "\033[36m" : "";
    const char *col_dim   = cfg->color ? "\033[2m" : "";
    const char *col_reset = cfg->color ? "\033[0m" : "";

    if (cfg->format == OUTPUT_BYTES) {
        if (show_path) {
            printf("%llu\t%s\n", (unsigned long long)size_val, path);
        } else {
            printf("%llu\n", (unsigned long long)size_val);
        }
        return;
    }

    if (cfg->format == OUTPUT_KB) {
        double kb = (double)size_val / (cfg->si_units ? 1000.0 : 1024.0);
        if (show_path) {
            printf("%.2f KB\t%s\n", kb, path);
        } else {
            printf("%.2f KB\n", kb);
        }
        return;
    }

    if (cfg->format == OUTPUT_MB) {
        double base = cfg->si_units ? 1000.0 : 1024.0;
        double mb = (double)size_val / (base * base);
        if (show_path) {
            printf("%.2f MB\t%s\n", mb, path);
        } else {
            printf("%.2f MB\n", mb);
        }
        return;
    }

    if (cfg->format == OUTPUT_GB) {
        double base = cfg->si_units ? 1000.0 : 1024.0;
        double gb = (double)size_val / (base * base * base);
        if (show_path) {
            printf("%.2f GB\t%s\n", gb, path);
        } else {
            printf("%.2f GB\n", gb);
        }
        return;
    }

    if (cfg->format == OUTPUT_SHORT) {
        char size_buf[64];
        format_bytes(size_val, size_buf, sizeof(size_buf), cfg->si_units);
        if (show_path) {
            printf("%s\t%s\n", size_buf, path);
        } else {
            printf("%s\n", size_buf);
        }
        return;
    }

    if (cfg->format == OUTPUT_JSON) {
        printf("{\n");
        printf("  \"path\": \"%s\",\n", path);
        printf("  \"type\": \"%s\",\n", res->is_directory ? "directory" : "file");
        printf("  \"apparent_size_bytes\": %llu,\n", (unsigned long long)res->apparent_size);
        printf("  \"disk_size_bytes\": %llu,\n", (unsigned long long)res->disk_size);
        printf("  \"file_count\": %llu,\n", (unsigned long long)res->file_count);
        printf("  \"dir_count\": %llu,\n", (unsigned long long)res->dir_count);
        printf("  \"symlink_count\": %llu,\n", (unsigned long long)res->symlink_count);
        printf("  \"other_count\": %llu,\n", (unsigned long long)res->other_count);
        printf("  \"error_count\": %llu,\n", (unsigned long long)res->error_count);
        printf("  \"elapsed_seconds\": %.6f,\n", res->elapsed_seconds);
        printf("  \"threads\": %d\n", res->threads_used);
        printf("}\n");
        return;
    }

    if (cfg->format == OUTPUT_VERBOSE) {
        char app_buf[64], disk_buf[64], app_num[64], disk_num[64];
        char files_num[64], dirs_num[64], links_num[64], dur_buf[64];

        format_bytes(res->apparent_size, app_buf, sizeof(app_buf), cfg->si_units);
        format_bytes(res->disk_size, disk_buf, sizeof(disk_buf), cfg->si_units);
        format_number(res->apparent_size, app_num, sizeof(app_num));
        format_number(res->disk_size, disk_num, sizeof(disk_num));
        format_number(res->file_count, files_num, sizeof(files_num));
        format_number(res->dir_count, dirs_num, sizeof(dirs_num));
        format_number(res->symlink_count, links_num, sizeof(links_num));
        format_duration(res->elapsed_seconds, dur_buf, sizeof(dur_buf));

        printf("%sTarget:%s         %s%s%s\n", col_bold, col_reset, col_cyan, path, col_reset);
        printf("%sType:%s           %s\n", col_bold, col_reset, res->is_directory ? "Directory" : "File");
        printf("%sApparent Size:%s  %s%s%s (%s bytes)\n", col_bold, col_reset, col_green, app_buf, col_reset, app_num);
        printf("%sDisk Allocated:%s %s (%s bytes)\n", col_bold, col_reset, disk_buf, disk_num);
        if (res->is_directory) {
            printf("%sFiles:%s          %s\n", col_bold, col_reset, files_num);
            printf("%sDirectories:%s    %s\n", col_bold, col_reset, dirs_num);
            if (res->symlink_count > 0) {
                printf("%sSymlinks:%s       %s\n", col_bold, col_reset, links_num);
            }
            if (res->other_count > 0) {
                printf("%sOther:%s          %llu\n", col_bold, col_reset, (unsigned long long)res->other_count);
            }
            if (res->error_count > 0) {
                printf("%sErrors:%s         %llu (inaccessible entries)\n", col_bold, col_reset, (unsigned long long)res->error_count);
            }
            printf("%sThreads:%s        %d worker threads\n", col_bold, col_reset, res->threads_used);
            printf("%sElapsed Time:%s   %s\n", col_bold, col_reset, dur_buf);
        }
        return;
    }

    /* DEFAULT OUTPUT */
    char size_str[64];
    char bytes_num[64];
    format_bytes(size_val, size_str, sizeof(size_str), cfg->si_units);
    format_number(size_val, bytes_num, sizeof(bytes_num));

    if (!res->is_directory) {
        /* Single file */
        if (show_path) {
            printf("%s%s%s (%s bytes)    %s\n",
                   col_green, size_str, col_reset, bytes_num, path);
        } else {
            printf("%s%s%s (%s bytes)\n",
                   col_green, size_str, col_reset, bytes_num);
        }
    } else {
        /* Directory */
        char files_str[64], dirs_str[64], dur_str[64];
        format_number(res->file_count, files_str, sizeof(files_str));
        format_number(res->dir_count, dirs_str, sizeof(dirs_str));
        format_duration(res->elapsed_seconds, dur_str, sizeof(dur_str));

        if (show_path) {
            printf("%s%s%s (%s bytes)    %-24s %s[%s files, %s dirs in %s]%s\n",
                   col_green, size_str, col_reset, bytes_num, path,
                   col_dim, files_str, dirs_str, dur_str, col_reset);
        } else {
            printf("%s%s%s (%s bytes)  %s[%s files, %s dirs in %s]%s\n",
                   col_green, size_str, col_reset, bytes_num,
                   col_dim, files_str, dirs_str, dur_str, col_reset);
        }
    }
}

int main(int argc, char *argv[]) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.format = OUTPUT_DEFAULT;
    cfg.disk_usage = false;
    cfg.si_units = false;
    cfg.color = isatty(STDOUT_FILENO) && (getenv("NO_COLOR") == NULL);
    cfg.scan_opts.num_threads = 0; /* Auto-detect */
    cfg.scan_opts.one_file_system = false;
    cfg.scan_opts.verbose = false;

    static struct option long_options[] = {
        {"bytes",           no_argument,       0, 'b'},
        {"kb",              no_argument,       0, 'k'},
        {"mb",              no_argument,       0, 'm'},
        {"gb",              no_argument,       0, 'g'},
        {"short",           no_argument,       0, 's'},
        {"disk",            no_argument,       0, 'd'},
        {"apparent",        no_argument,       0, 'a'},
        {"verbose",         no_argument,       0, 'v'},
        {"threads",         required_argument, 0, 't'},
        {"one-file-system", no_argument,       0, 'x'},
        {"si",              no_argument,       0, 1000},
        {"no-color",        no_argument,       0, 1001},
        {"json",            no_argument,       0, 1002},
        {"help",            no_argument,       0, 'h'},
        {"version",         no_argument,       0, 1003},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "bkmsgdavxt:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                cfg.format = OUTPUT_BYTES;
                break;
            case 'k':
                cfg.format = OUTPUT_KB;
                break;
            case 'm':
                cfg.format = OUTPUT_MB;
                break;
            case 'g':
                cfg.format = OUTPUT_GB;
                break;
            case 's':
                cfg.format = OUTPUT_SHORT;
                break;
            case 'd':
                cfg.disk_usage = true;
                break;
            case 'a':
                cfg.disk_usage = false;
                break;
            case 'v':
                cfg.format = OUTPUT_VERBOSE;
                cfg.scan_opts.verbose = true;
                break;
            case 't': {
                int t = atoi(optarg);
                if (t <= 0) {
                    fprintf(stderr, "sizeof: error: invalid thread count '%s'\n", optarg);
                    return 1;
                }
                cfg.scan_opts.num_threads = t;
                break;
            }
            case 'x':
                cfg.scan_opts.one_file_system = true;
                break;
            case 1000:
                cfg.si_units = true;
                break;
            case 1001:
                cfg.color = false;
                break;
            case 1002:
                cfg.format = OUTPUT_JSON;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            case 1003:
                print_version();
                return 0;
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
        }
    }

    int num_paths = argc - optind;
    const char *default_paths[] = {"."};
    const char **paths;

    if (num_paths == 0) {
        paths = default_paths;
        num_paths = 1;
    } else {
        paths = (const char **)&argv[optind];
    }

    bool show_path = (num_paths > 1);
    uint64_t total_apparent = 0;
    uint64_t total_disk = 0;
    uint64_t total_files = 0;
    uint64_t total_dirs = 0;
    int exit_code = 0;

    for (int i = 0; i < num_paths; i++) {
        const char *p = paths[i];
        ScanResult res;
        if (!scan_path(p, &cfg.scan_opts, &res)) {
            fprintf(stderr, "sizeof: cannot access '%s': No such file or directory\n", p);
            exit_code = 1;
            continue;
        }

        print_result(p, &res, &cfg, show_path);

        total_apparent += res.apparent_size;
        total_disk     += res.disk_size;
        total_files    += res.file_count;
        total_dirs     += res.dir_count;
    }

    /* Print total summary if multiple paths were specified */
    if (num_paths > 1 && cfg.format == OUTPUT_DEFAULT) {
        uint64_t total_val = cfg.disk_usage ? total_disk : total_apparent;
        char tot_size[64], tot_bytes[64], tot_files[64], tot_dirs[64];
        format_bytes(total_val, tot_size, sizeof(tot_size), cfg.si_units);
        format_number(total_val, tot_bytes, sizeof(tot_bytes));
        format_number(total_files, tot_files, sizeof(tot_files));
        format_number(total_dirs, tot_dirs, sizeof(tot_dirs));

        printf("--------------------------------------------------------------------------------\n");
        printf("Total: %s (%s bytes)  [%s files, %s dirs]\n",
               tot_size, tot_bytes, tot_files, tot_dirs);
    }

    return exit_code;
}
