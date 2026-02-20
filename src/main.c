#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "mrush.h"

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
    if (argc != 4) { usage(argv[0]); return EXIT_FAILURE; }

    uint32_t target_ini = (uint32_t)strtoul(argv[1], NULL, 10);
    int rounds = (int)strtol(argv[2], NULL, 10);
    int n_threads = (int)strtol(argv[3], NULL, 10);
    if (rounds <= 0 || n_threads <= 0) {
        fprintf(stderr, "ROUNDS and N_THREADS must be > 0\n");
        return EXIT_FAILURE;
    }

    int fd[2];
    if (pipe(fd) == -1) { perror("pipe"); return EXIT_FAILURE; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return EXIT_FAILURE; }

    if (pid == 0) {
        // LOGGER (hijo) - por ahora trivial
        close(fd[1]); // no escribe
        // En pasos siguientes: return logger_run(fd[0]);
        printf("Logger starting (trivial)\n");
        close(fd[0]);
        exit(EXIT_SUCCESS);
    }

    // MINER (padre) - por ahora trivial
    close(fd[0]); // no lee
    printf("Miner starting (trivial)\n");
    close(fd[1]); // importante: cerrar para que el logger vea EOF en versión real

    int st;
    waitpid(pid, &st, 0);
    print_child_status("Logger", st);

    // Mensaje propio
    printf("Miner exited with status %d\n", 0);
    return EXIT_SUCCESS;
}