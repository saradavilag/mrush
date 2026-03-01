#include <stdio.h>
#include "miner.h"
#include "logger.h"
#include "pow.h"

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
        // Hijo = Registrador
        close(fd[1]); // no escribe
        int status = logger_run(fd[0], fd[1]);
        close(fd[0]);
        exit(status);
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