#include <stdio.h>
#include <sys/wait.h>
#include "miner.h"

int main(int argc, char *argv[]) {

    if (argc != 4) {
        fprintf(stderr,"Usage: ./miner <TARGET_INI> <ROUNDS> <N_THREADS>\n");
        return EXIT_FAILURE;
    }

    uint32_t target_ini = (uint32_t)strtoul(argv[1], NULL, 10);
    int rounds = (int)strtol(argv[2], NULL, 10);
    int n_threads = (int)strtol(argv[3], NULL, 10);

    int m2l[2]; // miner -> logger
    int l2m[2]; // logger -> miner

    if (pipe(m2l) == -1) {
        perror("pipe m2l");
        return EXIT_FAILURE;
    }

    if (pipe(l2m) == -1) {
        perror("pipe l2m");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    /* ================= LOGGER ================= */
    if (pid == 0) {

        close(m2l[1]); // no escribe hacia logger
        close(l2m[0]); // no lee ACK

        int status = logger_run(m2l[0], l2m[1]);

        close(m2l[0]);
        close(l2m[1]);

        exit(status);
    }

    /* ================= MINER ================= */
    close(m2l[0]); // no lee
    close(l2m[1]); // no escribe ACK

    if (miner_run(m2l[1], l2m[0],
                  target_ini, rounds, n_threads) == EXIT_FAILURE)
        return EXIT_FAILURE;

    close(m2l[1]);
    close(l2m[0]);

    int st;
    waitpid(pid, &st, 0);

    if (WIFEXITED(st))
        printf("Logger exited with status %d\n", WEXITSTATUS(st));
    else
        printf("Logger exited unexpectedly\n");

    printf("Miner exited with status 0\n");
    return EXIT_SUCCESS;
}