#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "miner.h"
#include "types.h"
#include "pow.h"

/* Worker thread: busca una solución en [start, end).
 * - Lee found de forma atómica (sin mutex)
 * - Si encuentra solución, intenta "ganar" con CAS found: 0 -> 1
 * - Solo el ganador escribe *solution
 */
static void *worker(void *arg) {
    worker_args *a = (worker_args *)arg;

    // Campos que ya no se usan con el enfoque atómico (no tocamos types.h)
    (void)a->mutex;
    (void)a->round;
    (void)a->write_fd;

    for (uint32_t i = a->start; i < a->end; i++) {

        // Si alguien ya encontró, salimos (lectura atómica)
        int already = __atomic_load_n(a->found, __ATOMIC_ACQUIRE);
        if (already) break;

        // pow_hash trabaja con long int
        long int h = pow_hash((long int)i);

        if (h == (long int)a->target) {
            // Intentar convertirnos en el ganador: found pasa de 0 a 1
            int expected = 0;
            if (__atomic_compare_exchange_n(
                    a->found, &expected, 1,
                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                *(a->solution) = i;
            }
            break;
        }
    }

    return NULL;
}

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

int miner_run(int write_fd, int read_fd, uint32_t target_ini, int rounds, int n_threads) {
    (void)read_fd; // si lo usáis para “votes/wallets”, lo integraréis aquí; de momento no se usa

    if (rounds <= 0 || n_threads <= 0) {
        fprintf(stderr, "miner_run: rounds and n_threads must be > 0\n");
        return EXIT_FAILURE;
    }

    uint32_t target = target_ini;

    pthread_t *tids = calloc((size_t)n_threads, sizeof(*tids));
    worker_args *args = calloc((size_t)n_threads, sizeof(*args));
    if (!tids || !args) {
        perror("calloc");
        free(tids);
        free(args);
        return EXIT_FAILURE;
    }

    for (int round = 1; round <= rounds; round++) {

        uint32_t solution = 0;
        int found = 0;

        // Particionamos [0, POW_LIMIT) en n_threads trozos.
        uint32_t base = (uint32_t)POW_LIMIT / (uint32_t)n_threads;
        uint32_t rem  = (uint32_t)POW_LIMIT % (uint32_t)n_threads;

        uint32_t start = 0;

        for (int k = 0; k < n_threads; k++) {
            uint32_t chunk = base + ((uint32_t)k < rem ? 1u : 0u);
            uint32_t end = start + chunk;

            args[k].start = start;
            args[k].end = end;
            args[k].target = target;

            args[k].solution = &solution;
            args[k].found = &found;

            // Ya no usamos mutex, pero mantenemos el campo para compatibilidad
            args[k].mutex = NULL;

            // Estos campos ya no los usa el worker (write lo hace el main)
            args[k].round = round;
            args[k].write_fd = write_fd;

            int err = pthread_create(&tids[k], NULL, worker, &args[k]);
            if (err != 0) {
                fprintf(stderr, "pthread_create: %s\n", strerror(err));
                for (int j = 0; j < k; j++) pthread_join(tids[j], NULL);
                free(tids);
                free(args);
                return EXIT_FAILURE;
            }

            start = end;
        }

        for (int k = 0; k < n_threads; k++) {
            int err = pthread_join(tids[k], NULL);
            if (err != 0) {
                fprintf(stderr, "pthread_join: %s\n", strerror(err));
                free(tids);
                free(args);
                return EXIT_FAILURE;
            }
        }

        if (!__atomic_load_n(&found, __ATOMIC_ACQUIRE)) {
            fprintf(stderr, "Round %d: no solution found for target %08u\n", round, target);
            // Si preferís abortar:
            // free(tids); free(args); return EXIT_FAILURE;
        }

        // Validación: mantenemos accepted=1 por defecto (ajustad si tenéis lógica real)
        int valid = 1;
        if (valid) {
            printf("Solution accepted : %08u --> %08u\n", target, solution);
        } else {
            printf("Solution rejected : %08u --> %08u\n", target, solution);
        }

        log_args msg;
        msg.round = round;
        msg.target = target;
        msg.solution = solution;
        msg.valid = valid;

        if (write_all(write_fd, &msg, sizeof(msg)) != 0) {
            perror("write to logger pipe");
            free(tids);
            free(args);
            return EXIT_FAILURE;
        }

        target = solution;
    }

    free(tids);
    free(args);
    return EXIT_SUCCESS;
}