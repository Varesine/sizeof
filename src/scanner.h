#ifndef SIZEOF_SCANNER_H
#define SIZEOF_SCANNER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "progress.h"

typedef struct {
    int num_threads;
    bool one_file_system;
    bool verbose;
    ProgressState *progress;
} ScanOptions;

typedef struct {
    uint64_t apparent_size;  /* Sum of st_size in bytes */
    uint64_t disk_size;      /* Sum of st_blocks * 512 in bytes */
    uint64_t file_count;     /* Count of regular files */
    uint64_t dir_count;      /* Count of directories */
    uint64_t symlink_count;  /* Count of symbolic links */
    uint64_t other_count;    /* Sockets, pipes, character/block devices */
    uint64_t error_count;    /* Unreadable or permission denied */
    double elapsed_seconds;  /* Wall clock time in seconds */
    int threads_used;        /* Actual threads utilized */
    bool is_directory;       /* True if target was a directory */
    bool exists;             /* True if target exists and was accessible */
} ScanResult;

/* Scans target path (file or directory).
 * If target is a folder, traverses recursively using multithreading.
 * Returns true on success, false if path could not be accessed.
 */
bool scan_path(const char *path, const ScanOptions *opts, ScanResult *result);

#endif /* SIZEOF_SCANNER_H */
