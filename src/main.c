/**
 * @file main.c
 * @author Sara, Marco
 * @date 2026-03-04
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

#include <stdio.h>
#include <stdlib.h>   /* EXIT_SUCCESS/FAILURE, strtoul/strtol */
#include <stdint.h>   /* uint32_t */
#include <unistd.h>   /* pipe, fork, close */
#include <sys/wait.h> /* waitpid, WIFEXITED, WEXITSTATUS */

#include "miner.h"

int main(int argc, char *argv[]) {

    /* Validación de argumentos según enunciado:
    ./miner <TARGET_INI> <ROUNDS> <N_THREADS> */
    if (argc != 4) {
        fprintf(stderr, "Usage: ./miner <TARGET_INI> <ROUNDS> <N_THREADS>\n");
        return EXIT_FAILURE;
    }

    /* Parseo de parámetros:
    - TARGET_INI es uint32_t
    - ROUNDS y N_THREADS son enteros */
    uint32_t target_ini = (uint32_t)strtoul(argv[1], NULL, 10);
    int rounds = (int)strtol(argv[2], NULL, 10);
    int n_threads = (int)strtol(argv[3], NULL, 10);

    /* Tuberías:
    m2l: canal Miner -> Logger (envío de log_args)
    l2m: canal Logger -> Miner (ACK opcional; en esta versión no la usamos) */
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

    /* ----------------- LOGGER (proceso hijo) -------------- */
    if (pid == 0) {

        /* Cerrar extremos no utilizados para evitar bloqueos y fugas de FDs:
        - El logger solo lee de m2l[0], por tanto cierra m2l[1].
        - Si no usamos ACK, también podemos cerrar el extremo de lectura del canal inverso. */
        close(m2l[1]); // el logger no escribe al canal miner->logger
        close(l2m[0]); // el logger no lee ACK (canal inverso)

        int status = logger_run(m2l[0], l2m[1]);

        /* Cierre explicito antes de terminar */
        close(m2l[0]);
        close(l2m[1]);

        exit(status);
    }

    /*-------------------- MINER (proceso padre) --------------------- */
    close(m2l[0]); // el minero no lee del canal miner->logger
    close(l2m[1]); // el minero no escribe ACK

    /* Ejecutar minado (sale EXIT_FAILURE si hay errores internos) */
    if (miner_run(m2l[1], l2m[0], target_ini, rounds, n_threads) == EXIT_FAILURE)
        return EXIT_FAILURE;

    /* Señal de EOF al logger: cerrar extremo de escritura */
    close(m2l[1]);
    close(l2m[0]);

    /* Esperar al proceso logger y reportar estado */
    int st;
    waitpid(pid, &st, 0);

    if (WIFEXITED(st))
        printf("Logger exited with status %d\n", WEXITSTATUS(st));
    else
        printf("Logger exited unexpectedly\n");

    printf("Miner exited with status 0\n");
    return EXIT_SUCCESS;
}