#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MINERS_FILE "/tmp/mrush_miners.txt"
#define MINERS_SEM_NAME "/mrush_miners_mutex"
#define MAX_MINERS 1024

static volatile sig_atomic_t stop_flag = 0;

static void handle_alarm(int sig) {
    (void)sig;
    stop_flag = 1;
}

static sem_t *open_mutex(void) {
    sem_t *sem = sem_open(MINERS_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return NULL;
    }
    return sem;
}

static int read_pids(pid_t pids[], size_t max_pids, size_t *count) {
    FILE *f;
    pid_t pid;
    size_t used = 0;

    *count = 0;
    f = fopen(MINERS_FILE, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        perror("fopen miners file (read)");
        return -1;
    }

    while (fscanf(f, "%d", &pid) == 1) {
        if (used < max_pids) {
            pids[used++] = pid;
        } else {
            fprintf(stderr, "Too many miners in system file\n");
            fclose(f);
            return -1;
        }
    }

    if (!feof(f)) {
        perror("fscanf miners file");
        fclose(f);
        return -1;
    }

    fclose(f);
    *count = used;
    return 0;
}

static int write_pids(const pid_t pids[], size_t count) {
    FILE *f = fopen(MINERS_FILE, "w");
    if (f == NULL) {
        perror("fopen miners file (write)");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (fprintf(f, "%d\n", (int)pids[i]) < 0) {
            perror("fprintf miners file");
            fclose(f);
            return -1;
        }
    }

    if (fclose(f) != 0) {
        perror("fclose miners file");
        return -1;
    }
    return 0;
}

static bool pid_in_list(const pid_t pids[], size_t count, pid_t pid) {
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == pid) {
            return true;
        }
    }
    return false;
}

static void print_miners_list(const pid_t pids[], size_t count) {
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

static int add_current_miner(sem_t *mutex, pid_t self) {
    pid_t pids[MAX_MINERS];
    size_t count = 0;

    if (sem_wait(mutex) == -1) {
        perror("sem_wait");
        return -1;
    }

    if (read_pids(pids, MAX_MINERS, &count) == -1) {
        sem_post(mutex);
        return -1;
    }

    if (!pid_in_list(pids, count, self)) {
        if (count >= MAX_MINERS) {
            fprintf(stderr, "Too many miners\n");
            sem_post(mutex);
            return -1;
        }
        pids[count++] = self;
        if (write_pids(pids, count) == -1) {
            sem_post(mutex);
            return -1;
        }
    }

    printf("Miner %d added to system\n", (int)self);
    print_miners_list(pids, count);

    if (sem_post(mutex) == -1) {
        perror("sem_post");
        return -1;
    }
    return 0;
}

static int remove_current_miner(sem_t *mutex, pid_t self) {
    pid_t pids[MAX_MINERS];
    pid_t kept[MAX_MINERS];
    size_t count = 0;
    size_t kept_count = 0;

    if (sem_wait(mutex) == -1) {
        perror("sem_wait");
        return -1;
    }

    if (read_pids(pids, MAX_MINERS, &count) == -1) {
        sem_post(mutex);
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (pids[i] != self) {
            kept[kept_count++] = pids[i];
        }
    }

    if (kept_count == 0) {
        if (unlink(MINERS_FILE) == -1 && errno != ENOENT) {
            perror("unlink miners file");
            sem_post(mutex);
            return -1;
        }
    } else if (write_pids(kept, kept_count) == -1) {
        sem_post(mutex);
        return -1;
    }

    printf("Miner %d exited system\n", (int)self);
    print_miners_list(kept, kept_count);

    if (sem_post(mutex) == -1) {
        perror("sem_post");
        return -1;
    }

    if (kept_count == 0) {
        if (sem_unlink(MINERS_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink");
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    long n_secs;
    long n_threads;
    struct sigaction act;
    sem_t *mutex;
    pid_t self = getpid();

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <N_SECS> <N_THREADS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    n_secs = strtol(argv[1], NULL, 10);
    n_threads = strtol(argv[2], NULL, 10);
    if (n_secs <= 0 || n_threads <= 0) {
        fprintf(stderr, "Both <N_SECS> and <N_THREADS> must be > 0\n");
        return EXIT_FAILURE;
    }

    (void)n_threads; /* apartado a: se valida pero aún no se usa */

    memset(&act, 0, sizeof(act));
    act.sa_handler = handle_alarm;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    mutex = open_mutex();
    if (mutex == NULL) {
        return EXIT_FAILURE;
    }

    if (add_current_miner(mutex, self) == -1) {
        sem_close(mutex);
        return EXIT_FAILURE;
    }

    alarm((unsigned int)n_secs);
    while (!stop_flag) {
        pause();
    }

    if (remove_current_miner(mutex, self) == -1) {
        sem_close(mutex);
        return EXIT_FAILURE;
    }

    if (sem_close(mutex) == -1) {
        perror("sem_close");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}