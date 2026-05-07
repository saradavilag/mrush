/**
 * @file miner.h
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Interfaz del proceso Minero (Miner) del proyecto Miner Rush.
 *
 * Este módulo expone la función principal del Minero, encargada de resolver 
 * pruebas de esfuerzo (POW) usando múltiples hilos POSIX, y coordinarse con 
 * el resto de la red mediante Memoria Compartida y Colas de Mensajes.
 */

#ifndef MINER_H
#define MINER_H

#include <mqueue.h>    /* mqd_t */
#include "monitor.h"   /* SharedData */

/**
 * @brief Ejecuta el ciclo de minado principal integrado con la red global.
 *
 * Lee el objetivo de la memoria compartida, reparte el rango entre hilos, 
 * y si encuentra solución, la envía al Comprobador por la cola de mensajes.
 * Informa al Logger local (vía tubería) de los resultados tras la validación de la red.
 *
 * @param write_fd Descriptor de la tubería para enviar datos al proceso Logger local.
 * @param shm Puntero al bloque de memoria compartida (SharedData).
 * @param mq Descriptor de la cola de mensajes hacia el Comprobador.
 * @param mi_indice Posición del minero en active_miners[] de la memoria compartida.
 * @param n_threads Número de hilos lanzados por ronda de minado.
 * @return EXIT_SUCCESS si finaliza correctamente, EXIT_FAILURE ante cualquier error.
 */
int miner_run(int write_fd, SharedData *shm, mqd_t mq, int mi_indice, int n_threads);

/**
 * @brief Registra al minero en la red de memoria compartida.
 *
 * Busca una posición libre en el array active_miners de la estructura SharedData,
 * almacena su PID e incrementa el contador global protegiendo el acceso.
 *
 * @param shm Puntero al bloque de memoria compartida.
 * @return El índice asignado en la red (>=0), o -1 si la red está llena.
 */
int miner_add_system(SharedData *shm);

/**
 * @brief Elimina al minero de la red de memoria compartida y gestiona el cierre.
 *
 * Libera la posición del minero, decrementa el contador global y, si es el 
 * último minero activo en la red, envía un mensaje de finalización al Comprobador.
 *
 * @param shm Puntero al bloque de memoria compartida.
 * @param mq Descriptor de la cola de mensajes hacia el Comprobador.
 * @param mi_indice El índice asignado al minero durante el registro.
 * @return EXIT_SUCCESS si se eliminó correctamente, EXIT_FAILURE si hay error.
 */
int miner_del_system(SharedData *shm, mqd_t mq, int mi_indice);

#endif /* MINER_H */