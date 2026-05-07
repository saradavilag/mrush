/**
 * @file types.h
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Tipos y estructuras de datos compartidas por Miner y Logger.
 *
 * Este fichero centraliza las estructuras que se intercambian por IPC (pipe) y las que
 * se usan para parametrizar la ejecución de los hilos.
 *
 * Decisiones de diseño (P3):
 *  - log_args define el "mensaje" que se envía del Minero al Registrador local por tubería,
 *    conteniendo ahora información validada por la red (winner_pid, monedas reales, etc).
 *  - worker_args define estrictamente lo necesario para la búsqueda del POW, eliminando
 *    parámetros obsoletos de la P1/P2 al usar operaciones atómicas.
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h> /* Necesario para pid_t */

/**
 * @struct log_args
 * @brief Mensaje intercambiado entre Minero y su Logger local a través de la tubería.
 *
 * Contiene información real de la red para registrar una ronda:
 */
typedef struct _log_args {
    int round;          /** Número de ronda (o -1 para finalizar el logger) */
    pid_t winner_pid;   /** PID del proceso que ha ganado realmente la ronda */
    uint32_t target;    /** Objetivo de la ronda */
    uint32_t solution;  /** Solución encontrada y procesada */
    int valid;          /** 1 = validated, 0 = rejected */
    int votes_yes;      /** Número de votos a favor ('Y') recibidos */
    int total_votes;    /** Número total de votos emitidos en la ronda */
    int coins;          /** Monedas reales de este minero en la memoria compartida */
} log_args;

/**
 * @struct worker_args
 * @brief Argumentos de trabajo estrictamente necesarios para un hilo del Minero.
 *
 * Cada hilo explora un subrango del espacio [0, POW_LIMIT).
 * Se utiliza sincronización sin bloqueos (lock-free) mediante atómicos sobre 'found'.
 */
typedef struct _worker_args {
    uint32_t start;     /** Inicio del subrango (incluido) */
    uint32_t end;       /** Fin del subrango (excluido) */
    uint32_t target;    /** Target actual de la red */

    uint32_t *solution; /** Puntero a la variable de la solución propuesta */
    int *found;         /** Flag atómico: 1 si algún hilo ya encontró solución */
} worker_args;

#endif /* TYPES_H */