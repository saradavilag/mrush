#include "miner.h"
#include "pow.h"

void *worker(void *arg)
{
    worker_args_t *args = (worker_args_t *)arg;

    for (uint32_t i = args->start; i < args->end; i++) {

        /* Si otro hilo ya encontró solución, salir */
        if (*(args->found))
            break;

        if (pow_hash(i) == args->target) {

            /* Funcion para asegurar el buen funcionamiento concurrente de los hilos */
            pthread_mutex_lock(args->mutex);

            if (!*(args->found)) {
                *(args->solution) = i;
                *(args->found) = 1;
            }

            pthread_mutex_unlock(args->mutex);
            break;
        }
    }

    return NULL;
}

int miner_run(int write_fd, uint32_t target_ini, int rounds, int n_threads)
{
    (void)write_fd;   // aún no usado (parte c)

    pthread_t threads[n_threads];
    worker_args_t args[n_threads];

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    uint32_t target = target_ini;
    uint32_t solution = 0;
    int found;

    uint32_t chunk = POW_LIMIT / n_threads;

    for (int r = 1; r <= rounds; r++) {

        found = 0;

        /* dividir espacio de búsqueda */
        for (int i = 0; i < n_threads; i++) {

            args[i].start = i * chunk;

            if (i == n_threads - 1)
                args[i].end = POW_LIMIT;
            else
                args[i].end = (i + 1) * chunk;

            args[i].target = target;
            args[i].solution = &solution;
            args[i].found = &found;
            args[i].mutex = &mutex;

            int ret = pthread_create(&threads[i], NULL, worker, &args[i]);
            if (ret != 0) {
                perror("pthread_create");
                return EXIT_FAILURE;
            }
        }

        /* esperar a todos los hilos */
        for (int i = 0; i < n_threads; i++) {
            pthread_join(threads[i], NULL);
        }

        if (!found) {
            fprintf(stderr, "No solution found\n");
            return EXIT_FAILURE;
        }

        /* imprimir resultado de la ronda */
        printf("Solution accepted : %08u --> %08u\n",
               target, solution);

        /* siguiente ronda usa la solución como target */
        target = solution;
    }

    pthread_mutex_destroy(&mutex);

    printf("Miner finished correctly\n");
    return EXIT_SUCCESS;
}

