#ifndef SIZEOF_PROGRESS_H
#define SIZEOF_PROGRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct ProgressState {
    atomic_uint_fast64_t apparent_size;
    atomic_uint_fast64_t disk_size;
    atomic_uint_fast64_t items_count;
    atomic_bool active;
    bool enabled;
    bool color;
    bool si_units;
    double start_time;
    pthread_t thread;
} ProgressState;

/* Initializes progress state */
void progress_init(ProgressState *ps, bool enabled, bool color, bool si_units);

/* Starts the background progress display thread */
void progress_start(ProgressState *ps);

/* Stops the progress display thread and clears the progress line from stderr */
void progress_stop(ProgressState *ps);

#endif /* SIZEOF_PROGRESS_H */
