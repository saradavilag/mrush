#include "miner.h"

void *worker(void *arg)
{
    worker_args *args = (worker_args *)arg;

    for (uint32_t i = args->start; i < args->end; i++) {

        pthread_mutex_lock(args->mutex);
        int already_found = *(args->found);
        pthread_mutex_unlock(args->mutex);

        if (already_found)
            break;

        if (pow_hash(i) == args->target) {

            pthread_mutex_lock(args->mutex);

            if (!*(args->found)) {

                *(args->solution) = i;
                *(args->found) = 1;

                log_args msg;
                msg.round = args->round;
                msg.target = args->target;
                msg.solution = i;
                msg.valid = 1;

                if(write(args->write_fd, &msg, sizeof(msg)) != sizeof(msg)){
                    perror("write pipe");
                }
            }

            pthread_mutex_unlock(args->mutex);
            break;
        }
    }

    return NULL;
}

int miner_run(int write_fd, int read_fd, uint32_t target_ini, int rounds, int n_threads)
{ 
    pthread_t threads[n_threads];
    worker_args args[n_threads];

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
            args[i].round = r;
            args[i].write_fd = write_fd;

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
        
        /* Leemos la respuesta del logger */
        int ack;
        if (read(read_fd, &ack, sizeof(ack)) <= 0) {
            perror("read ack");
            return EXIT_FAILURE;
        }

        /* siguiente ronda usa la solución como target */
        target = solution;
    }

    pthread_mutex_destroy(&mutex);

    return EXIT_SUCCESS;
}

