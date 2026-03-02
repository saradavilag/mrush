#ifndef MINER_H
#define MINER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "pthread.h"
#include "logger.h"
#include "pow.h"

int miner_run(int write_fd, int read_fd, uint32_t target_ini, int rounds, int n_threads);

#endif