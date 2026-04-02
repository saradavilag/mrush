/**
 * @file miner.h
 * @author Sara, Marco
 * @date 2026-02-27
 * @brief Interfaz del proceso Minero (Miner) del proyecto Miner Rush.
 *
 * Este módulo expone la función principal del Minero, encargada de resolver un número
 * de pruebas de esfuerzo (POW) usando múltiples hilos POSIX y comunicarse con el Logger
 * mediante una tubería.
 *
 * Dependencias:
 *  - pthread: creación y sincronización de hilos.
 *  - logger.h/types.h: formato del mensaje enviado al Logger.
 *  - pow.h: función pow_hash() y constante POW_LIMIT.
 */

 #ifndef MINER_H
 #define MINER_H

 #define FILENAME "PID_miners.txt"
 
 #include <stdio.h>      /* fprintf, perror */
 #include <stdlib.h>     /* EXIT_SUCCESS, EXIT_FAILURE */
 #include <stdint.h>     /* uint32_t */
 #include <unistd.h>     /* write, close */
 
 #include <pthread.h>    /* pthread_create, pthread_join, etc */
 #include <semaphore.h>
 
 #include "logger.h"     /* logger_run (en caso de necesitar en main) / log_args */
 #include "pow.h"        /* pow_hash, POW_LIMIT */
 
 /**
  * @brief Ejecuta el proceso Minero.
  *
  * El Minero realiza un número de rondas de minado. En cada ronda:
  *  - Divide el espacio de búsqueda [0, POW_LIMIT) entre @p n_threads hilos.
  *  - Lanza los hilos y espera a que terminen.
  *  - Obtiene la solución s tal que pow_hash(s) == target.
  *  - Envía al Logger (via write_fd) un mensaje con target, solución y estado de validación.
  *  - Establece como nuevo target la solución encontrada para la siguiente ronda.
  *
  * Control de errores:
  *  - Debe devolver EXIT_FAILURE ante errores de creación/join de hilos o escritura en la tubería.
  *
  * @param write_fd Descriptor para escribir mensajes al Logger.
  * @param read_fd Descriptor para leer ACK del Logger (si se usa, opcional).
  * @param target_ini Objetivo inicial (target) de la ronda 1.
  * @param rounds Número de rondas a ejecutar.
  * @param n_threads Número de hilos a utilizar en cada ronda.
  * @return EXIT_SUCCESS si todo termina correctamente, EXIT_FAILURE en caso de error.
  *
  * @note Si el diseño no usa ACK, read_fd puede ignorarse dentro de la implementación.
  */
 int miner_run(int write_fd, int read_fd, uint32_t target_ini, int rounds, int n_threads);
 
 #endif /* MINER_H */