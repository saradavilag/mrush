#ifndef MANAGERS_H
#define MANAGERS_H

#include <stdint.h>
#include <semaphore.h>
#include <sys/types.h>

#define MINERS_FILE "/tmp/mrush_miners.txt"
#define TARGET_FILE "/tmp/mrush_target.txt"

#define MINERS_SEM_NAME "/mrush_miners_mutex"
#define TARGET_SEM_NAME "/mrush_target_mutex"

#define MAX_MINERS 1024

int managers_open_all(sem_t **miners_sem, sem_t **target_sem);
int managers_close_all(sem_t *miners_sem, sem_t *target_sem);

int managers_add_miner(sem_t *miners_sem, pid_t self, int *is_first_miner);
int managers_remove_miner(sem_t *miners_sem, pid_t self);

int managers_read_pids(sem_t *miners_sem, pid_t pids[], size_t max_pids, size_t *count);
void managers_print_miners(const pid_t pids[], size_t count);

int target_init_if_needed(sem_t *target_sem);
int target_read(sem_t *target_sem, uint32_t *target);
int target_write(sem_t *target_sem, uint32_t target);
int target_remove_if_exists(sem_t *target_sem);

#endif