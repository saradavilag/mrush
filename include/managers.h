#ifndef MANAGERS_H
#define MANAGERS_H

#include <stdint.h>
#include <semaphore.h>
#include <sys/types.h>

#define MINERS_FILE "/tmp/mrush_miners.txt"
#define TARGET_FILE "/tmp/mrush_target.txt"
#define VOTES_FILE  "/tmp/mrush_votes.txt"

#define MINERS_SEM_NAME "/mrush_miners_mutex"
#define TARGET_SEM_NAME "/mrush_target_mutex"
#define VOTES_SEM_NAME  "/mrush_votes_mutex"
#define WINNER_SEM_NAME "/mrush_winner_mutex"

#define MAX_MINERS 1024
#define MAX_VOTES  1024

typedef struct {
    uint32_t round;
    uint32_t target;
    pid_t winner;
    uint32_t solution;
} round_info_t;

int managers_open_all(sem_t **miners_sem, sem_t **target_sem,
                      sem_t **votes_sem, sem_t **winner_sem);
int managers_close_all(sem_t *miners_sem, sem_t *target_sem,
                       sem_t *votes_sem, sem_t *winner_sem);

int managers_add_miner(sem_t *miners_sem, pid_t self, int *is_first_miner);
int managers_remove_miner(sem_t *miners_sem, pid_t self);

int managers_read_pids(sem_t *miners_sem, pid_t pids[], size_t max_pids, size_t *count);
void managers_print_miners(const pid_t pids[], size_t count);

int target_init_if_needed(sem_t *target_sem);
int target_read_info(sem_t *target_sem, round_info_t *info);
int target_write_info(sem_t *target_sem, const round_info_t *info);
int target_remove_if_exists(sem_t *target_sem);

int votes_reset(sem_t *votes_sem);
int votes_add(sem_t *votes_sem, char vote);
int votes_read(sem_t *votes_sem, char votes[], size_t max_votes,
               size_t *count, int *yes, int *no);
int votes_remove_if_exists(sem_t *votes_sem);

#endif