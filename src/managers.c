#include "managers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int read_pids_unlocked(pid_t pids[], size_t max_pids, size_t *count) {
    FILE *f;
    pid_t pid;
    size_t used = 0;

    *count = 0;

    f = fopen(MINERS_FILE, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        perror("fopen MINERS_FILE (read)");
        return -1;
    }

    while (fscanf(f, "%d", &pid) == 1) {
        if (used >= max_pids) {
            fprintf(stderr, "Too many miners in system file\n");
            fclose(f);
            return -1;
        }
        pids[used++] = pid;
    }

    if (!feof(f)) {
        perror("fscanf MINERS_FILE");
        fclose(f);
        return -1;
    }

    fclose(f);
    *count = used;
    return 0;
}

static int write_pids_unlocked(const pid_t pids[], size_t count) {
    FILE *f = fopen(MINERS_FILE, "w");
    if (f == NULL) {
        perror("fopen MINERS_FILE (write)");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (fprintf(f, "%d\n", (int)pids[i]) < 0) {
            perror("fprintf MINERS_FILE");
            fclose(f);
            return -1;
        }
    }

    if (fclose(f) != 0) {
        perror("fclose MINERS_FILE");
        return -1;
    }

    return 0;
}

static int pid_in_list(const pid_t pids[], size_t count, pid_t pid) {
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == pid) {
            return 1;
        }
    }
    return 0;
}

int managers_open_all(sem_t **miners_sem, sem_t **target_sem,
                      sem_t **votes_sem, sem_t **winner_sem) {
    *miners_sem = sem_open(MINERS_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (*miners_sem == SEM_FAILED) {
        perror("sem_open MINERS_SEM_NAME");
        return -1;
    }

    *target_sem = sem_open(TARGET_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (*target_sem == SEM_FAILED) {
        perror("sem_open TARGET_SEM_NAME");
        sem_close(*miners_sem);
        return -1;
    }

    *votes_sem = sem_open(VOTES_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (*votes_sem == SEM_FAILED) {
        perror("sem_open VOTES_SEM_NAME");
        sem_close(*miners_sem);
        sem_close(*target_sem);
        return -1;
    }

    *winner_sem = sem_open(WINNER_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (*winner_sem == SEM_FAILED) {
        perror("sem_open WINNER_SEM_NAME");
        sem_close(*miners_sem);
        sem_close(*target_sem);
        sem_close(*votes_sem);
        return -1;
    }

    return 0;
}

int managers_close_all(sem_t *miners_sem, sem_t *target_sem,
                       sem_t *votes_sem, sem_t *winner_sem) {
    if (miners_sem != NULL && sem_close(miners_sem) == -1) {
        perror("sem_close miners_sem");
        return -1;
    }
    if (target_sem != NULL && sem_close(target_sem) == -1) {
        perror("sem_close target_sem");
        return -1;
    }
    if (votes_sem != NULL && sem_close(votes_sem) == -1) {
        perror("sem_close votes_sem");
        return -1;
    }
    if (winner_sem != NULL && sem_close(winner_sem) == -1) {
        perror("sem_close winner_sem");
        return -1;
    }
    return 0;
}

void managers_print_miners(const pid_t pids[], size_t count) {
    printf("Current miners:");
    if (count == 0) {
        printf(" (none)");
    } else {
        for (size_t i = 0; i < count; ++i) {
            printf(" %d", (int)pids[i]);
        }
    }
    printf("\n");
    fflush(stdout);
}

int managers_add_miner(sem_t *miners_sem, pid_t self, int *is_first_miner) {
    pid_t pids[MAX_MINERS];
    size_t count = 0;

    if (sem_wait(miners_sem) == -1) {
        perror("sem_wait miners_sem");
        return -1;
    }

    if (read_pids_unlocked(pids, MAX_MINERS, &count) == -1) {
        sem_post(miners_sem);
        return -1;
    }

    *is_first_miner = (count == 0);

    if (!pid_in_list(pids, count, self)) {
        if (count >= MAX_MINERS) {
            fprintf(stderr, "Too many miners\n");
            sem_post(miners_sem);
            return -1;
        }
        pids[count++] = self;

        if (write_pids_unlocked(pids, count) == -1) {
            sem_post(miners_sem);
            return -1;
        }
    }

    printf("Miner %d added to system\n", (int)self);
    managers_print_miners(pids, count);

    if (sem_post(miners_sem) == -1) {
        perror("sem_post miners_sem");
        return -1;
    }

    return 0;
}

int managers_remove_miner(sem_t *miners_sem, pid_t self) {
    pid_t pids[MAX_MINERS];
    pid_t kept[MAX_MINERS];
    size_t count = 0;
    size_t kept_count = 0;

    if (sem_wait(miners_sem) == -1) {
        perror("sem_wait miners_sem");
        return -1;
    }

    if (read_pids_unlocked(pids, MAX_MINERS, &count) == -1) {
        sem_post(miners_sem);
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (pids[i] != self) {
            kept[kept_count++] = pids[i];
        }
    }

    if (kept_count == 0) {
        if (unlink(MINERS_FILE) == -1 && errno != ENOENT) {
            perror("unlink MINERS_FILE");
            sem_post(miners_sem);
            return -1;
        }
    } else {
        if (write_pids_unlocked(kept, kept_count) == -1) {
            sem_post(miners_sem);
            return -1;
        }
    }

    printf("Miner %d exited system\n", (int)self);
    managers_print_miners(kept, kept_count);

    if (sem_post(miners_sem) == -1) {
        perror("sem_post miners_sem");
        return -1;
    }

    if (kept_count == 0) {
        target_remove_if_exists(NULL);
        votes_remove_if_exists(NULL);

        if (sem_unlink(MINERS_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink MINERS_SEM_NAME");
            return -1;
        }
        if (sem_unlink(TARGET_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink TARGET_SEM_NAME");
            return -1;
        }
        if (sem_unlink(VOTES_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink VOTES_SEM_NAME");
            return -1;
        }
        if (sem_unlink(WINNER_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink WINNER_SEM_NAME");
            return -1;
        }
    }

    return 0;
}

int managers_read_pids(sem_t *miners_sem, pid_t pids[], size_t max_pids, size_t *count) {
    if (sem_wait(miners_sem) == -1) {
        perror("sem_wait miners_sem");
        return -1;
    }

    if (read_pids_unlocked(pids, max_pids, count) == -1) {
        sem_post(miners_sem);
        return -1;
    }

    if (sem_post(miners_sem) == -1) {
        perror("sem_post miners_sem");
        return -1;
    }

    return 0;
}

int target_init_if_needed(sem_t *target_sem) {
    FILE *f;
    round_info_t info = {1, 0, -1, 0};

    if (sem_wait(target_sem) == -1) {
        perror("sem_wait target_sem");
        return -1;
    }

    f = fopen(TARGET_FILE, "r");
    if (f == NULL) {
        if (errno != ENOENT) {
            perror("fopen TARGET_FILE (read)");
            sem_post(target_sem);
            return -1;
        }

        f = fopen(TARGET_FILE, "w");
        if (f == NULL) {
            perror("fopen TARGET_FILE (write)");
            sem_post(target_sem);
            return -1;
        }

        if (fprintf(f, "%u %u %d %u\n",
                    info.round, info.target, (int)info.winner, info.solution) < 0) {
            perror("fprintf TARGET_FILE");
            fclose(f);
            sem_post(target_sem);
            return -1;
        }

        if (fclose(f) != 0) {
            perror("fclose TARGET_FILE");
            sem_post(target_sem);
            return -1;
        }
    } else {
        fclose(f);
    }

    if (sem_post(target_sem) == -1) {
        perror("sem_post target_sem");
        return -1;
    }

    return 0;
}

int target_read_info(sem_t *target_sem, round_info_t *info) {
    FILE *f;
    unsigned int round, target, solution;
    int winner;

    if (sem_wait(target_sem) == -1) {
        perror("sem_wait target_sem");
        return -1;
    }

    f = fopen(TARGET_FILE, "r");
    if (f == NULL) {
        perror("fopen TARGET_FILE (read)");
        sem_post(target_sem);
        return -1;
    }

    if (fscanf(f, "%u %u %d %u", &round, &target, &winner, &solution) != 4) {
        fprintf(stderr, "Invalid target file format\n");
        fclose(f);
        sem_post(target_sem);
        return -1;
    }

    fclose(f);

    info->round = round;
    info->target = target;
    info->winner = (pid_t)winner;
    info->solution = solution;

    if (sem_post(target_sem) == -1) {
        perror("sem_post target_sem");
        return -1;
    }

    return 0;
}

int target_write_info(sem_t *target_sem, const round_info_t *info) {
    FILE *f;

    if (sem_wait(target_sem) == -1) {
        perror("sem_wait target_sem");
        return -1;
    }

    f = fopen(TARGET_FILE, "w");
    if (f == NULL) {
        perror("fopen TARGET_FILE (write)");
        sem_post(target_sem);
        return -1;
    }

    if (fprintf(f, "%u %u %d %u\n",
                info->round, info->target, (int)info->winner, info->solution) < 0) {
        perror("fprintf TARGET_FILE");
        fclose(f);
        sem_post(target_sem);
        return -1;
    }

    if (fclose(f) != 0) {
        perror("fclose TARGET_FILE");
        sem_post(target_sem);
        return -1;
    }

    if (sem_post(target_sem) == -1) {
        perror("sem_post target_sem");
        return -1;
    }

    return 0;
}

int target_remove_if_exists(sem_t *target_sem) {
    int locked = 0;

    if (target_sem != NULL) {
        if (sem_wait(target_sem) == -1) {
            perror("sem_wait target_sem");
            return -1;
        }
        locked = 1;
    }

    if (unlink(TARGET_FILE) == -1 && errno != ENOENT) {
        perror("unlink TARGET_FILE");
        if (locked) sem_post(target_sem);
        return -1;
    }

    if (locked && sem_post(target_sem) == -1) {
        perror("sem_post target_sem");
        return -1;
    }

    return 0;
}

int votes_reset(sem_t *votes_sem) {
    FILE *f;

    if (votes_sem != NULL && sem_wait(votes_sem) == -1) {
        perror("sem_wait votes_sem");
        return -1;
    }

    f = fopen(VOTES_FILE, "w");
    if (f == NULL) {
        perror("fopen VOTES_FILE (write)");
        if (votes_sem != NULL) sem_post(votes_sem);
        return -1;
    }

    if (fclose(f) != 0) {
        perror("fclose VOTES_FILE");
        if (votes_sem != NULL) sem_post(votes_sem);
        return -1;
    }

    if (votes_sem != NULL && sem_post(votes_sem) == -1) {
        perror("sem_post votes_sem");
        return -1;
    }

    return 0;
}

int votes_add(sem_t *votes_sem, char vote) {
    FILE *f;

    if (sem_wait(votes_sem) == -1) {
        perror("sem_wait votes_sem");
        return -1;
    }

    f = fopen(VOTES_FILE, "a");
    if (f == NULL) {
        perror("fopen VOTES_FILE (append)");
        sem_post(votes_sem);
        return -1;
    }

    if (fprintf(f, "%c\n", vote) < 0) {
        perror("fprintf VOTES_FILE");
        fclose(f);
        sem_post(votes_sem);
        return -1;
    }

    if (fclose(f) != 0) {
        perror("fclose VOTES_FILE");
        sem_post(votes_sem);
        return -1;
    }

    if (sem_post(votes_sem) == -1) {
        perror("sem_post votes_sem");
        return -1;
    }

    return 0;
}

int votes_read(sem_t *votes_sem, char votes[], size_t max_votes,
               size_t *count, int *yes, int *no) {
    FILE *f;
    char v;
    size_t used = 0;
    int y = 0, n = 0;

    *count = 0;
    *yes = 0;
    *no = 0;

    if (sem_wait(votes_sem) == -1) {
        perror("sem_wait votes_sem");
        return -1;
    }

    f = fopen(VOTES_FILE, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            sem_post(votes_sem);
            return 0;
        }
        perror("fopen VOTES_FILE (read)");
        sem_post(votes_sem);
        return -1;
    }

    while (fscanf(f, " %c", &v) == 1) {
        if (used < max_votes) {
            votes[used++] = v;
        }
        if (v == 'Y') ++y;
        else if (v == 'N') ++n;
    }

    fclose(f);

    if (sem_post(votes_sem) == -1) {
        perror("sem_post votes_sem");
        return -1;
    }

    *count = used;
    *yes = y;
    *no = n;
    return 0;
}

int votes_remove_if_exists(sem_t *votes_sem) {
    int locked = 0;

    if (votes_sem != NULL) {
        if (sem_wait(votes_sem) == -1) {
            perror("sem_wait votes_sem");
            return -1;
        }
        locked = 1;
    }

    if (unlink(VOTES_FILE) == -1 && errno != ENOENT) {
        perror("unlink VOTES_FILE");
        if (locked) sem_post(votes_sem);
        return -1;
    }

    if (locked && sem_post(votes_sem) == -1) {
        perror("sem_post votes_sem");
        return -1;
    }

    return 0;
}