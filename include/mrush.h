#ifndef MRUSH_H
#define MRUSH_H

#include <stdint.h>

typedef struct {
    int32_t round;      // 1..ROUNDS
    uint32_t target;    // target de esa ronda
    uint32_t solution;  // solución encontrada
    int32_t valid;      // 1 validated, 0 rejected
} mrush_msg_t;

int miner_run(int write_fd, uint32_t target_ini, int rounds, int n_threads);
int logger_run(int read_fd);

#endif