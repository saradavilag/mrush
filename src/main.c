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
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "managers.h"

static volatile sig_atomic_t stop_flag = 0;
static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigusr2 = 0;

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

static int send_sigusr1_to_others(sem_t *miners_sem, pid_t self) {
    pid_t pids[MAX_MINERS];
    size_t count = 0;

    if (managers_read_pids(miners_sem, pids, MAX_MINERS, &count) == -1) {
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (pids[i] != self) {
            if (kill(pids[i], SIGUSR1) == -1) {
                if (errno != ESRCH) {
                    perror("kill SIGUSR1");
                    return -1;
                }
            }
        }
    }

    return 0;
}

static int current_miner_count(sem_t *miners_sem, size_t *count) {
    pid_t pids[MAX_MINERS];
    return managers_read_pids(miners_sem, pids, MAX_MINERS, count);
}

int main(int argc, char *argv[]) {
    long n_secs;
    long n_threads;
    pid_t self = getpid();
    sem_t *miners_sem = NULL;
    sem_t *target_sem = NULL;
    int is_first_miner = 0;
    int first_round_started = 0;
    uint32_t target = 0;
    size_t count = 0;

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

    if (install_handler(SIGALRM, handle_alarm) == -1) {
        return EXIT_FAILURE;
    }

    if (install_handler(SIGUSR1, handle_sigusr1) == -1) {
        return EXIT_FAILURE;
    }

    if (install_handler(SIGUSR2, handle_sigusr2) == -1) {
        return EXIT_FAILURE;
    }

    if (managers_open_all(&miners_sem, &target_sem) == -1) {
        return EXIT_FAILURE;
    }

    if (managers_add_miner(miners_sem, self, &is_first_miner) == -1) {
        managers_close_all(miners_sem, target_sem);
        return EXIT_FAILURE;
    }

    if (is_first_miner) {
        if (target_init_if_needed(target_sem) == -1) {
            managers_remove_miner(miners_sem, self);
            managers_close_all(miners_sem, target_sem);
            return EXIT_FAILURE;
        }
    }

    alarm((unsigned int)n_secs);

    while (!stop_flag) {
        if (current_miner_count(miners_sem, &count) == -1) {
            break;
        }

        if (count < 2) {
            usleep(200000);
            continue;
        }

        if (is_first_miner && !first_round_started) {
            if (send_sigusr1_to_others(miners_sem, self) == -1) {
                break;
            }
            got_sigusr1 = 1;
            first_round_started = 1;
        }

        if (!got_sigusr1) {
            pause();
            continue;
        }

        got_sigusr1 = 0;

        if (target_read(target_sem, &target) == -1) {
            break;
        }

        printf("Miner %d starts round with target %u and %ld threads\n",
               (int)self, target, n_threads);
        fflush(stdout);

        /*
         * Fase actual del apartado b):
         * solo arrancamos ronda y demostramos que todos reciben SIGUSR1
         * y leen el mismo target.
         *
         * La lógica de ganador + votación vendrá después.
         */
        pause();
    }

    if (managers_remove_miner(miners_sem, self) == -1) {
        managers_close_all(miners_sem, target_sem);
        return EXIT_FAILURE;
    }

    if (managers_close_all(miners_sem, target_sem) == -1) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}