#define _POSIX_C_SOURCE 200809L
#include "progress.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static bool is_utf8_terminal(void) {
    const char *lang = getenv("LANG");
    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");
    if (lc_all && (strstr(lc_all, "UTF-8") || strstr(lc_all, "utf8"))) return true;
    if (lc_ctype && (strstr(lc_ctype, "UTF-8") || strstr(lc_ctype, "utf8"))) return true;
    if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8"))) return true;
    return true; /* Modern macOS default is UTF-8 */
}

void progress_init(ProgressState *ps, bool enabled, bool color, bool si_units) {
    if (!ps) return;
    atomic_init(&ps->apparent_size, 0);
    atomic_init(&ps->disk_size, 0);
    atomic_init(&ps->items_count, 0);
    atomic_init(&ps->active, false);
    ps->enabled = enabled;
    ps->color = color;
    ps->si_units = si_units;
    ps->start_time = 0.0;
}

static void *progress_worker(void *arg) {
    ProgressState *ps = (ProgressState *)arg;

    /* Initial 80ms delay: if scan completes in <80ms, avoid unnecessary screen flicker */
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 80 * 1000 * 1000;
    nanosleep(&delay, NULL);

    if (!atomic_load(&ps->active)) {
        return NULL;
    }

    bool utf8 = is_utf8_terminal();
    const char *utf8_spinners[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const char *ascii_spinners[] = {"|", "/", "-", "\\"};
    const char **spinners = utf8 ? utf8_spinners : ascii_spinners;
    int num_spinners = utf8 ? 10 : 4;
    int spin_idx = 0;

    const int bar_width = 16;
    const int block_size = 4;
    int slider_pos = 0;
    int slider_dir = 1;
    bool has_drawn = false;

    while (atomic_load(&ps->active)) {
        uint64_t bytes = atomic_load_explicit(&ps->apparent_size, memory_order_relaxed);
        uint64_t items = atomic_load_explicit(&ps->items_count, memory_order_relaxed);
        double now = get_time_sec();
        double elapsed = now - ps->start_time;

        char size_buf[32];
        format_bytes(bytes, size_buf, sizeof(size_buf), ps->si_units);

        char items_buf[32];
        format_number(items, items_buf, sizeof(items_buf));

        /* Construct animated progress bar */
        char bar[128];
        int bpos = 0;
        bar[bpos++] = '[';
        for (int i = 0; i < bar_width; i++) {
            if (i >= slider_pos && i < slider_pos + block_size) {
                if (utf8) {
                    /* Solid block "█" (0xE2, 0x96, 0x88) */
                    bar[bpos++] = '\xe2';
                    bar[bpos++] = '\x96';
                    bar[bpos++] = '\x88';
                } else {
                    bar[bpos++] = '=';
                }
            } else {
                if (utf8) {
                    /* Light shade block "░" (0xE2, 0x96, 0x91) */
                    bar[bpos++] = '\xe2';
                    bar[bpos++] = '\x96';
                    bar[bpos++] = '\x91';
                } else {
                    bar[bpos++] = ' ';
                }
            }
        }
        bar[bpos++] = ']';
        bar[bpos] = '\0';

        slider_pos += slider_dir;
        if (slider_pos + block_size >= bar_width) {
            slider_dir = -1;
        } else if (slider_pos <= 0) {
            slider_dir = 1;
        }

        const char *spinner = spinners[spin_idx % num_spinners];
        spin_idx++;

        if (ps->color) {
            fprintf(stderr, "\r\033[2K\033[36m%s\033[0m \033[1mScanning...\033[0m \033[34m%s\033[0m \033[32m%s\033[0m | \033[33m%s items\033[0m \033[2m(%.1fs)\033[0m",
                    spinner, bar, size_buf, items_buf, elapsed);
        } else {
            fprintf(stderr, "\r\033[2K%s Scanning... %s %s | %s items (%.1fs)",
                    spinner, bar, size_buf, items_buf, elapsed);
        }
        fflush(stderr);
        has_drawn = true;

        /* 50ms refresh period (~20 FPS) */
        delay.tv_sec = 0;
        delay.tv_nsec = 50 * 1000 * 1000;
        nanosleep(&delay, NULL);
    }

    if (has_drawn) {
        /* Erase the progress line cleanly */
        fprintf(stderr, "\r\033[2K");
        fflush(stderr);
    }

    return NULL;
}

void progress_start(ProgressState *ps) {
    if (!ps || !ps->enabled) return;
    ps->start_time = get_time_sec();
    atomic_store(&ps->active, true);
    pthread_create(&ps->thread, NULL, progress_worker, ps);
}

void progress_stop(ProgressState *ps) {
    if (!ps || !ps->enabled) return;
    if (atomic_load(&ps->active)) {
        atomic_store(&ps->active, false);
        pthread_join(ps->thread, NULL);
    }
}
