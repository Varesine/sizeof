#ifndef SIZEOF_UTILS_H
#define SIZEOF_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Formats byte count to human-readable string (e.g. "1.45 GB" or "1.45 GiB") */
void format_bytes(uint64_t bytes, char *out, size_t out_len, bool si_units);

/* Formats an integer with commas (e.g. 1557239812 -> "1,557,239,812") */
void format_number(uint64_t num, char *out, size_t out_len);

/* High-resolution monotonic timestamp in seconds */
double get_time_sec(void);

/* Formats elapsed duration (e.g. "45 ms" or "1.23s") */
void format_duration(double seconds, char *out, size_t out_len);

#endif /* SIZEOF_UTILS_H */
