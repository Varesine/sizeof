#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "scanner.h"
#include "utils.h"
#include "progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define INODE_CACHE_BUCKETS 1024

/* Node for hard-link deduplication */
typedef struct InodeNode {
    dev_t dev;
    ino_t ino;
    struct InodeNode *next;
} InodeNode;

typedef struct {
    InodeNode *buckets[INODE_CACHE_BUCKETS];
    pthread_mutex_t locks[INODE_CACHE_BUCKETS];
} InodeCache;

/* Work item for directory queue */
typedef struct WorkItem {
    char *path;
    struct WorkItem *next;
} WorkItem;

/* Shared scanner context */
typedef struct {
    const ScanOptions *opts;
    dev_t root_dev;

    /* Directory work queue */
    WorkItem *queue_head;
    WorkItem *queue_tail;
    size_t queue_size;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;
    int active_workers;
    bool shutdown;

    /* Hard link deduplication cache */
    InodeCache inode_cache;

    /* Final accumulated stats */
    pthread_mutex_t stats_lock;
    uint64_t total_apparent_size;
    uint64_t total_disk_size;
    uint64_t total_file_count;
    uint64_t total_dir_count;
    uint64_t total_symlink_count;
    uint64_t total_other_count;
    uint64_t total_error_count;
} ScannerContext;

/* Per-thread stats to eliminate stats lock contention during scanning */
typedef struct {
    uint64_t apparent_size;
    uint64_t disk_size;
    uint64_t file_count;
    uint64_t dir_count;
    uint64_t symlink_count;
    uint64_t other_count;
    uint64_t error_count;
} ThreadLocalStats;

/* Hash function for (dev, ino) pair */
static inline uint32_t hash_inode(dev_t dev, ino_t ino) {
    uint64_t h = ((uint64_t)dev << 32) ^ (uint64_t)ino;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (uint32_t)(h % INODE_CACHE_BUCKETS);
}

static void inode_cache_init(InodeCache *cache) {
    for (int i = 0; i < INODE_CACHE_BUCKETS; i++) {
        cache->buckets[i] = NULL;
        pthread_mutex_init(&cache->locks[i], NULL);
    }
}

/* Returns true if inode was already in cache, or adds it and returns false */
static bool inode_cache_check_and_add(InodeCache *cache, dev_t dev, ino_t ino) {
    uint32_t idx = hash_inode(dev, ino);
    pthread_mutex_lock(&cache->locks[idx]);

    InodeNode *curr = cache->buckets[idx];
    while (curr) {
        if (curr->dev == dev && curr->ino == ino) {
            pthread_mutex_unlock(&cache->locks[idx]);
            return true; /* already seen */
        }
        curr = curr->next;
    }

    InodeNode *node = malloc(sizeof(InodeNode));
    if (node) {
        node->dev = dev;
        node->ino = ino;
        node->next = cache->buckets[idx];
        cache->buckets[idx] = node;
    }

    pthread_mutex_unlock(&cache->locks[idx]);
    return false;
}

static void inode_cache_destroy(InodeCache *cache) {
    for (int i = 0; i < INODE_CACHE_BUCKETS; i++) {
        InodeNode *curr = cache->buckets[i];
        while (curr) {
            InodeNode *tmp = curr->next;
            free(curr);
            curr = tmp;
        }
        cache->buckets[i] = NULL;
        pthread_mutex_destroy(&cache->locks[i]);
    }
}

/* Worker thread function */
static void *scanner_worker(void *arg) {
    ScannerContext *ctx = (ScannerContext *)arg;
    ThreadLocalStats local_stats = {0};
    char *current_path = NULL;
    uint64_t delta_apparent = 0;
    uint64_t delta_disk = 0;
    uint64_t delta_items = 0;

    while (1) {
        /* Grab a path from the queue if we don't already hold one */
        if (current_path == NULL) {
            pthread_mutex_lock(&ctx->queue_lock);
            while (ctx->queue_head == NULL && !ctx->shutdown) {
                if (ctx->active_workers == 0) {
                    /* All workers are idle and queue is empty: scan is finished */
                    ctx->shutdown = true;
                    pthread_cond_broadcast(&ctx->queue_cond);
                    break;
                }
                pthread_cond_wait(&ctx->queue_cond, &ctx->queue_lock);
            }

            if (ctx->shutdown) {
                pthread_mutex_unlock(&ctx->queue_lock);
                break;
            }

            WorkItem *item = ctx->queue_head;
            ctx->queue_head = item->next;
            if (ctx->queue_head == NULL) {
                ctx->queue_tail = NULL;
            }
            ctx->queue_size--;
            current_path = item->path;
            free(item);

            ctx->active_workers++;
            pthread_mutex_unlock(&ctx->queue_lock);
        }

        /* Scan current_path */
        DIR *dir = opendir(current_path);
        if (!dir) {
            local_stats.error_count++;
            free(current_path);
            current_path = NULL;

            pthread_mutex_lock(&ctx->queue_lock);
            ctx->active_workers--;
            if (ctx->active_workers == 0 && ctx->queue_head == NULL) {
                ctx->shutdown = true;
                pthread_cond_broadcast(&ctx->queue_cond);
            }
            pthread_mutex_unlock(&ctx->queue_lock);
            continue;
        }

        int dfd = dirfd(dir);
        size_t path_len = strlen(current_path);
        bool need_slash = (path_len > 0 && current_path[path_len - 1] != '/');

        char **subdirs = NULL;
        size_t subdirs_cnt = 0;
        size_t subdirs_cap = 0;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (name[0] == '.') {
                if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) {
                    continue; /* skip . and .. */
                }
            }

            struct stat st;
            if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                local_stats.error_count++;
                continue;
            }

            /* Cross-filesystem check */
            if (ctx->opts->one_file_system && st.st_dev != ctx->root_dev) {
                continue;
            }

            /* Hard link deduplication for non-directories with st_nlink > 1 */
            if (!S_ISDIR(st.st_mode) && st.st_nlink > 1) {
                if (inode_cache_check_and_add(&ctx->inode_cache, st.st_dev, st.st_ino)) {
                    continue; /* already counted */
                }
            }

            /* Accumulate sizes */
            local_stats.apparent_size += (uint64_t)st.st_size;
            local_stats.disk_size += (uint64_t)st.st_blocks * 512;

            /* Progress accounting batching */
            delta_apparent += (uint64_t)st.st_size;
            delta_disk     += (uint64_t)st.st_blocks * 512;
            delta_items++;

            if (delta_items >= 128) {
                if (ctx->opts && ctx->opts->progress) {
                    atomic_fetch_add_explicit(&ctx->opts->progress->apparent_size, delta_apparent, memory_order_relaxed);
                    atomic_fetch_add_explicit(&ctx->opts->progress->disk_size, delta_disk, memory_order_relaxed);
                    atomic_fetch_add_explicit(&ctx->opts->progress->items_count, delta_items, memory_order_relaxed);
                }
                delta_apparent = 0;
                delta_disk = 0;
                delta_items = 0;
            }

            if (S_ISDIR(st.st_mode)) {
                local_stats.dir_count++;

                /* Construct full child path */
                size_t name_len = strlen(name);
                size_t child_len = path_len + (need_slash ? 1 : 0) + name_len + 1;
                char *child_path = malloc(child_len);
                if (child_path) {
                    snprintf(child_path, child_len, "%s%s%s", current_path, need_slash ? "/" : "", name);
                    if (subdirs_cnt >= subdirs_cap) {
                        subdirs_cap = (subdirs_cap == 0) ? 16 : (subdirs_cap * 2);
                        char **new_subdirs = realloc(subdirs, subdirs_cap * sizeof(char *));
                        if (new_subdirs) {
                            subdirs = new_subdirs;
                        } else {
                            free(child_path);
                            child_path = NULL;
                        }
                    }
                    if (child_path) {
                        subdirs[subdirs_cnt++] = child_path;
                    }
                }
            } else if (S_ISREG(st.st_mode)) {
                local_stats.file_count++;
            } else if (S_ISLNK(st.st_mode)) {
                local_stats.symlink_count++;
            } else {
                local_stats.other_count++;
            }
        }

        closedir(dir);

        if (delta_items > 0) {
            if (ctx->opts && ctx->opts->progress) {
                atomic_fetch_add_explicit(&ctx->opts->progress->apparent_size, delta_apparent, memory_order_relaxed);
                atomic_fetch_add_explicit(&ctx->opts->progress->disk_size, delta_disk, memory_order_relaxed);
                atomic_fetch_add_explicit(&ctx->opts->progress->items_count, delta_items, memory_order_relaxed);
            }
            delta_apparent = 0;
            delta_disk = 0;
            delta_items = 0;
        }

        free(current_path);
        current_path = NULL;

        /* Depth-first optimization: keep the first subdirectory for this thread */
        if (subdirs_cnt > 0) {
            current_path = subdirs[0];
        }

        /* Push remaining subdirectories to work queue */
        pthread_mutex_lock(&ctx->queue_lock);
        if (subdirs_cnt > 1) {
            for (size_t i = 1; i < subdirs_cnt; i++) {
                WorkItem *item = malloc(sizeof(WorkItem));
                if (item) {
                    item->path = subdirs[i];
                    item->next = NULL;
                    if (ctx->queue_tail) {
                        ctx->queue_tail->next = item;
                        ctx->queue_tail = item;
                    } else {
                        ctx->queue_head = item;
                        ctx->queue_tail = item;
                    }
                    ctx->queue_size++;
                } else {
                    free(subdirs[i]);
                }
            }
            pthread_cond_broadcast(&ctx->queue_cond);
        }

        if (current_path == NULL) {
            /* No direct work left for this thread */
            ctx->active_workers--;
            if (ctx->active_workers == 0 && ctx->queue_head == NULL) {
                ctx->shutdown = true;
                pthread_cond_broadcast(&ctx->queue_cond);
            }
        }
        pthread_mutex_unlock(&ctx->queue_lock);

        free(subdirs);
    }

    /* Merge thread-local stats into final accumulated stats */
    pthread_mutex_lock(&ctx->stats_lock);
    ctx->total_apparent_size += local_stats.apparent_size;
    ctx->total_disk_size     += local_stats.disk_size;
    ctx->total_file_count    += local_stats.file_count;
    ctx->total_dir_count     += local_stats.dir_count;
    ctx->total_symlink_count += local_stats.symlink_count;
    ctx->total_other_count   += local_stats.other_count;
    ctx->total_error_count   += local_stats.error_count;
    pthread_mutex_unlock(&ctx->stats_lock);

    return NULL;
}

bool scan_path(const char *path, const ScanOptions *opts, ScanResult *result) {
    if (!path || !result) return false;

    memset(result, 0, sizeof(ScanResult));
    double start_time = get_time_sec();

    struct stat st;
    if (lstat(path, &st) != 0) {
        result->exists = false;
        result->elapsed_seconds = get_time_sec() - start_time;
        return false;
    }

    result->exists = true;

    /* If it is not a directory, handle it as a single file directly without threads */
    if (!S_ISDIR(st.st_mode)) {
        result->is_directory = false;
        result->apparent_size = (uint64_t)st.st_size;
        result->disk_size = (uint64_t)st.st_blocks * 512;
        result->threads_used = 1;

        if (S_ISREG(st.st_mode)) {
            result->file_count = 1;
        } else if (S_ISLNK(st.st_mode)) {
            result->symlink_count = 1;
        } else {
            result->other_count = 1;
        }

        result->elapsed_seconds = get_time_sec() - start_time;
        return true;
    }

    /* Target is a directory: use multithreaded scanning */
    result->is_directory = true;

    int num_threads = opts ? opts->num_threads : 0;
    if (num_threads <= 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        num_threads = (cpus > 0) ? (int)cpus : 4;
    }
    if (num_threads > 128) {
        num_threads = 128;
    }
    result->threads_used = num_threads;

    ScannerContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.opts = opts;
    ctx.root_dev = st.st_dev;

    /* Include root directory's own size in the totals */
    ctx.total_apparent_size = (uint64_t)st.st_size;
    ctx.total_disk_size = (uint64_t)st.st_blocks * 512;
    ctx.total_dir_count = 1; /* Count root directory itself */

    pthread_mutex_init(&ctx.queue_lock, NULL);
    pthread_cond_init(&ctx.queue_cond, NULL);
    pthread_mutex_init(&ctx.stats_lock, NULL);
    inode_cache_init(&ctx.inode_cache);

    /* Enqueue root path */
    WorkItem *root_item = malloc(sizeof(WorkItem));
    root_item->path = strdup(path);
    root_item->next = NULL;
    ctx.queue_head = root_item;
    ctx.queue_tail = root_item;
    ctx.queue_size = 1;

    /* Launch worker threads */
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    if (!threads) {
        free(root_item->path);
        free(root_item);
        inode_cache_destroy(&ctx.inode_cache);
        pthread_mutex_destroy(&ctx.queue_lock);
        pthread_cond_destroy(&ctx.queue_cond);
        pthread_mutex_destroy(&ctx.stats_lock);
        return false;
    }

    /* Start background progress indicator if enabled */
    if (opts && opts->progress && opts->progress->enabled) {
        progress_start(opts->progress);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, scanner_worker, &ctx);
    }

    /* Wait for all workers to complete */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Stop progress indicator and erase progress line */
    if (opts && opts->progress && opts->progress->enabled) {
        progress_stop(opts->progress);
    }

    free(threads);

    /* Copy aggregated results */
    result->apparent_size = ctx.total_apparent_size;
    result->disk_size     = ctx.total_disk_size;
    result->file_count    = ctx.total_file_count;
    result->dir_count     = ctx.total_dir_count;
    result->symlink_count = ctx.total_symlink_count;
    result->other_count   = ctx.total_other_count;
    result->error_count   = ctx.total_error_count;
    result->elapsed_seconds = get_time_sec() - start_time;

    /* Cleanup remaining queue (should be empty) */
    WorkItem *curr = ctx.queue_head;
    while (curr) {
        WorkItem *tmp = curr->next;
        free(curr->path);
        free(curr);
        curr = tmp;
    }

    inode_cache_destroy(&ctx.inode_cache);
    pthread_mutex_destroy(&ctx.queue_lock);
    pthread_cond_destroy(&ctx.queue_cond);
    pthread_mutex_destroy(&ctx.stats_lock);

    return true;
}
