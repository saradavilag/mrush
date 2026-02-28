#ifndef MINER_H
#define MINER_H

#include <stdint.h>
#include "logger.h"
#include "pow.h"
#include "pthread.h"

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t target;

    uint32_t *solution;
    int *found;

    pthread_mutex_t *mutex;
} worker_args_t;

int miner_run(int write_fd, uint32_t target_ini, int rounds, int n_threads);

#endif