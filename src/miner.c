#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "miner.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <TARGET_INI> <ROUNDS> <N_THREADS>\n", prog);
}

static void print_child_status(const char *name, int status) {
    if (WIFEXITED(status)) {
        printf("%s exited with status %d\n", name, WEXITSTATUS(status));
    } else {
        printf("%s exited unexpectedly\n", name);
    }
}


int main(int argc, char *argv[]) {
    
    /* Comprobamos numero de argumentos */
    if (argc != 4) { 
        usage(argv[0]); 
        return EXIT_FAILURE; 
    }

    /* Guardamos los argumentos */
    uint32_t target_ini = (uint32_t)strtoul(argv[1], NULL, 10);
    int rounds = (int)strtol(argv[2], NULL, 10);
    int n_threads = (int)strtol(argv[3], NULL, 10);
    if (rounds <= 0 || n_threads <= 0) {
        fprintf(stderr, "ROUNDS and N_THREADS must be > 0\n");
        return EXIT_FAILURE;
    }

    /* Creamos las tuberias */
    int fd[2];

    if (pipe(fd) == -1) { 
        perror("pipe"); 
        return EXIT_FAILURE; 
    }

    /* Creamos nuevo proceso */
    pid_t pid = fork();
    if (pid < 0) { 
        perror("fork"); 
        return EXIT_FAILURE; 
    }

    /* Proceso hijo, REGISTRADOR */
    if (pid == 0) {
        // LOGGER (hijo) - por ahora trivial
        close(fd[1]); // no escribe
        // En pasos siguientes: return logger_run(fd[0]);
        printf("Logger starting (trivial)\n");
        close(fd[0]);
        exit(EXIT_SUCCESS);
    }

    /* Proceso padre, MINERO */
    close(fd[0]); // no lee
    if (miner_run(fd[1], target_ini, rounds, n_threads) == EXIT_FAILURE){
        return EXIT_FAILURE;
    }
    printf("Miner starting (trivial)\n");
    close(fd[1]); // importante: cerrar para que el logger vea EOF en versión real

    int st;
    waitpid(pid, &st, 0);
    print_child_status("Logger", st);

    // Mensaje propio
    printf("Miner exited with status %d\n", 0);
    return EXIT_SUCCESS;
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

void *worker(void *arg)
{
    worker_args_t *args = (worker_args_t *)arg;

    for (uint32_t i = args->start; i < args->end; i++) {

        /* Si otro hilo ya encontró solución, salir */
        if (*(args->found))
            break;

        if (pow_hash(i) == args->target) {

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