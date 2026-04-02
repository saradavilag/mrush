/**
 * @file main.c
 * @author Sara, Marco
 * @date 2026-04-02
 * @brief Punto de entrada del programa ./miner (Miner Rush - minero único).
 *
 * Responsabilidades:
 *  - Parsear argumentos de línea de comandos: TARGET_INI, ROUNDS, N_THREADS.
 *  - Crear las tuberías necesarias para la comunicación Miner -> Logger (y canal inverso).
 *  - Crear el proceso Logger con fork().
 *  - Ejecutar el minado (miner_run) en el proceso padre.
 *  - Esperar al proceso Logger y mostrar su código de salida según el enunciado.
 *
 * Dependencias:
 *  - miner.h: interfaz del minero.
 *  - logger.h: interfaz del logger (incluida por miner.h).
 *  - sys/wait.h: waitpid y macros WIFEXITED/WEXITSTATUS.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "managers.h"
#include "pow.h"

#define VOTE_WAIT_TRIES 30
#define VOTE_WAIT_USEC 100000

static volatile sig_atomic_t stop_flag = 0;
static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigusr2 = 0;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t target;
    uint32_t *solution;
    int *found;
    pthread_mutex_t *mutex;
} worker_args_t;

static void handle_alarm(int sig) {
    (void)sig;
    stop_flag = 1;
}

static void handle_sigusr1(int sig) {
    (void)sig;
    got_sigusr1 = 1;
}

static void handle_sigusr2(int sig) {
    (void)sig;
    got_sigusr2 = 1;
}

static int install_handler(int signum, void (*handler)(int)) {
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = 0;

    if (sigaction(signum, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

static void *worker(void *arg) {
    worker_args_t *a = (worker_args_t *)arg;

    for (uint32_t i = a->start; i < a->end; ++i) {
        if (stop_flag || got_sigusr2) {
            break;
        }

        if (__atomic_load_n(a->found, __ATOMIC_ACQUIRE)) {
            break;
        }

        if ((uint32_t)pow_hash((long int)i) == a->target) {
            pthread_mutex_lock(a->mutex);
            if (!*(a->found)) {
                *(a->found) = 1;
                *(a->solution) = i;
            }
            pthread_mutex_unlock(a->mutex);
            break;
        }
    }

    return NULL;
}

static int mine_solution(uint32_t target, int n_threads, uint32_t *solution) {
    pthread_t *tids = NULL;
    worker_args_t *args = NULL;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int found = 0;
    uint32_t base, rem, start = 0;

    tids = calloc((size_t)n_threads, sizeof(*tids));
    args = calloc((size_t)n_threads, sizeof(*args));
    if (tids == NULL || args == NULL) {
        perror("calloc");
        free(tids);
        free(args);
        return -1;
    }

    base = (uint32_t)POW_LIMIT / (uint32_t)n_threads;
    rem  = (uint32_t)POW_LIMIT % (uint32_t)n_threads;

    for (int k = 0; k < n_threads; ++k) {
        uint32_t chunk = base + ((uint32_t)k < rem ? 1U : 0U);
        uint32_t end = start + chunk;

        args[k].start = start;
        args[k].end = end;
        args[k].target = target;
        args[k].solution = solution;
        args[k].found = &found;
        args[k].mutex = &mutex;

        if (pthread_create(&tids[k], NULL, worker, &args[k]) != 0) {
            perror("pthread_create");
            for (int j = 0; j < k; ++j) pthread_join(tids[j], NULL);
            free(tids);
            free(args);
            return -1;
        }

        start = end;
    }

    for (int k = 0; k < n_threads; ++k) {
        pthread_join(tids[k], NULL);
    }

    free(tids);
    free(args);

    return found ? 1 : 0;
}

static int send_signal_to_list(const pid_t pids[], size_t count, pid_t self, int signum) {
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == self) continue;
        if (kill(pids[i], signum) == -1) {
            if (errno != ESRCH) {
                perror("kill");
                return -1;
            }
        }
    }
    return 0;
}

static int count_miners(sem_t *miners_sem, size_t *count, pid_t pids[]) {
    return managers_read_pids(miners_sem, pids, MAX_MINERS, count);
}

static int vote_validity(uint32_t target, uint32_t solution) {
    return ((uint32_t)pow_hash((long int)solution) == target) ? 'Y' : 'N';
}

static void print_votes_line(pid_t winner, const char votes[], size_t count, int accepted) {
    printf("Winner %d => [", (int)winner);
    for (size_t i = 0; i < count; ++i) {
        printf(" %c", votes[i]);
    }
    printf(" ] => %s\n", accepted ? "Accepted" : "Rejected");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    long n_secs;
    long n_threads;
    pid_t self = getpid();
    sem_t *miners_sem = NULL;
    sem_t *target_sem = NULL;
    sem_t *votes_sem = NULL;
    sem_t *winner_sem = NULL;
    int is_first_miner = 0;
    int started_any_round = 0;
    int coins = 0;
    pid_t participants[MAX_MINERS];
    size_t participants_count = 0;
    round_info_t info;

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

    if (install_handler(SIGALRM, handle_alarm) == -1) return EXIT_FAILURE;
    if (install_handler(SIGUSR1, handle_sigusr1) == -1) return EXIT_FAILURE;
    if (install_handler(SIGUSR2, handle_sigusr2) == -1) return EXIT_FAILURE;

    if (managers_open_all(&miners_sem, &target_sem, &votes_sem, &winner_sem) == -1) {
        return EXIT_FAILURE;
    }

    if (managers_add_miner(miners_sem, self, &is_first_miner) == -1) {
        managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
        return EXIT_FAILURE;
    }

    if (is_first_miner) {
        if (target_init_if_needed(target_sem) == -1) {
            managers_remove_miner(miners_sem, self);
            managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
            return EXIT_FAILURE;
        }
    }

    alarm((unsigned int)n_secs);

    while (!stop_flag) {
        size_t count = 0;

        if (count_miners(miners_sem, &count, participants) == -1) {
            break;
        }

        if (count < 2) {
            usleep(200000);
            continue;
        }

        if (is_first_miner && !started_any_round) {
            if (target_read_info(target_sem, &info) == -1) break;
            info.winner = -1;
            info.solution = 0;
            if (target_write_info(target_sem, &info) == -1) break;

            if (send_signal_to_list(participants, count, self, SIGUSR1) == -1) break;
            got_sigusr1 = 1;
            started_any_round = 1;
        }

        if (!got_sigusr1) {
            pause();
            continue;
        }

        got_sigusr1 = 0;
        got_sigusr2 = 0;

        if (count_miners(miners_sem, &participants_count, participants) == -1) {
            break;
        }

        if (participants_count < 2) {
            continue;
        }

        if (target_read_info(target_sem, &info) == -1) {
            break;
        }

        printf("Miner %d starts round %u with target %u and %ld threads\n",
               (int)self, info.round, info.target, n_threads);
        fflush(stdout);

        uint32_t solution = 0;
        int mine_res = mine_solution(info.target, (int)n_threads, &solution);
        if (mine_res == -1) break;

        int i_am_winner = 0;

        if (!got_sigusr2 && mine_res == 1) {
            if (sem_wait(winner_sem) == -1) {
                perror("sem_wait winner_sem");
                break;
            }

            round_info_t current;
            if (target_read_info(target_sem, &current) == -1) {
                sem_post(winner_sem);
                break;
            }

            if (current.round == info.round && current.winner == -1) {
                current.winner = self;
                current.solution = solution;

                if (target_write_info(target_sem, &current) == -1) {
                    sem_post(winner_sem);
                    break;
                }

                if (votes_reset(votes_sem) == -1) {
                    sem_post(winner_sem);
                    break;
                }

                i_am_winner = 1;
            }

            if (sem_post(winner_sem) == -1) {
                perror("sem_post winner_sem");
                break;
            }

            if (i_am_winner) {
                if (send_signal_to_list(participants, participants_count, self, SIGUSR2) == -1) {
                    break;
                }
                got_sigusr2 = 1;
            }
        }

        while (!got_sigusr2 && !stop_flag) {
            pause();
        }

        if (stop_flag) break;

        if (target_read_info(target_sem, &info) == -1) {
            break;
        }

        if (info.winner != -1) {
            char my_vote = (char)vote_validity(info.target, info.solution);
            if (votes_add(votes_sem, my_vote) == -1) {
                break;
            }
        }

        if (i_am_winner) {
            char votes[MAX_VOTES];
            size_t votes_count = 0;
            int yes = 0, no = 0;
            int tries = 0;
            int accepted = 0;

            while (tries < VOTE_WAIT_TRIES) {
                if (votes_read(votes_sem, votes, MAX_VOTES, &votes_count, &yes, &no) == -1) {
                    break;
                }

                if (votes_count >= participants_count) {
                    break;
                }

                usleep(VOTE_WAIT_USEC);
                ++tries;
            }

            accepted = (yes >= no);
            if (accepted) {
                ++coins;
            }

            print_votes_line(info.winner, votes, votes_count, accepted);

            round_info_t next = info;
            next.round = info.round + 1;
            next.winner = -1;
            next.solution = 0;
            if (accepted) {
                next.target = info.solution;
            }

            if (target_write_info(target_sem, &next) == -1) {
                break;
            }

            if (!stop_flag) {
                if (count_miners(miners_sem, &participants_count, participants) == -1) {
                    break;
                }

                if (participants_count >= 2) {
                    if (send_signal_to_list(participants, participants_count, self, SIGUSR1) == -1) {
                        break;
                    }
                    got_sigusr1 = 1;
                }
            }

            (void)coins;
        }
    }

    if (managers_remove_miner(miners_sem, self) == -1) {
        managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
        return EXIT_FAILURE;
    }

    if (managers_close_all(miners_sem, target_sem, votes_sem, winner_sem) == -1) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}