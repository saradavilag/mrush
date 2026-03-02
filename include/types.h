#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

/* Estructura para los registradores */
typedef struct _log_args{
    int round;          /* Indica el numero de ronda */
    uint32_t target;    /* Indica el objetivo de la ronda */
    uint32_t solution;  /* Indica la solucion encontrada */
    int valid;          /* Indica solucion validada (1), o rechazada (0)*/
} log_args;

/* Estructura para los hilos del minero */
typedef struct _worker_args{
    uint32_t start;
    uint32_t end;
    uint32_t target;

    uint32_t *solution;
    int *found;

    pthread_mutex_t *mutex;
    int round;
    int write_fd;
} worker_args;

