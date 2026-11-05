#define _POSIX_C_SOURCE 200809L
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void format_bytes(uint64_t bytes, char *out, size_t out_len, bool si_units) {
    if (!out || out_len == 0) return;

    const double base = si_units ? 1000.0 : 1024.0;
    const char *units_si[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB"};
    const char *units_bin[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    const char **units = si_units ? units_si : units_bin;
    const int max_unit = 6;

    if (bytes == 0) {
        snprintf(out, out_len, "0 B");
        return;
    }

    double val = (double)bytes;
    int u = 0;
    while (val >= base && u < max_unit) {
        val /= base;
        u++;
    }

    if (u == 0) {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    } else if (val >= 100.0) {
        snprintf(out, out_len, "%.1f %s", val, units[u]);
    } else if (val >= 10.0) {
        snprintf(out, out_len, "%.2f %s", val, units[u]);
    } else {
        snprintf(out, out_len, "%.2f %s", val, units[u]);
    }
}

void format_number(uint64_t num, char *out, size_t out_len) {
    if (!out || out_len == 0) return;

    char temp[64];
    int len = snprintf(temp, sizeof(temp), "%llu", (unsigned long long)num);
    if (len <= 0) {
        out[0] = '\0';
        return;
    }

    /* Count how many commas we need */
    int commas = (len - 1) / 3;
    int total_len = len + commas;
    if ((size_t)total_len >= out_len) {
        /* Fallback if buffer is too small */
        snprintf(out, out_len, "%s", temp);
        return;
    }

    out[total_len] = '\0';
    int src = len - 1;
    int dst = total_len - 1;
    int count = 0;

    while (src >= 0) {
        if (count > 0 && count % 3 == 0) {
            out[dst--] = ',';
        }
        out[dst--] = temp[src--];
        count++;
    }
}

double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void format_duration(double seconds, char *out, size_t out_len) {
    if (!out || out_len == 0) return;

    if (seconds < 0.001) {
        snprintf(out, out_len, "<1 ms");
    } else if (seconds < 1.0) {
        snprintf(out, out_len, "%.1f ms", seconds * 1000.0);
    } else if (seconds < 60.0) {
        snprintf(out, out_len, "%.2fs", seconds);
    } else {
        int minutes = (int)(seconds / 60.0);
        double rem = seconds - (minutes * 60.0);
        snprintf(out, out_len, "%dm %.1fs", minutes, rem);
    }
}
