/**
 * @file types.h
 * @author Sara, Marco
 * @date 2026-03-04
 * @brief Tipos y estructuras de datos compartidas por Miner y Logger.
 *
 * Este fichero centraliza las estructuras que se intercambian por IPC (pipe) y las que
 * se usan para parametrizar la ejecución de los hilos.
 *
 * Decisiones de diseño:
 *  - log_args define el "mensaje" que se envía del Minero al Registrador por la tubería.
 *  - worker_args define el rango de búsqueda asignado a cada hilo del Minero.
 *
 * Dependencias:
 *  - stdint.h para tipos fijos (uint32_t)
 *  - pthread.h para tipos de sincronización (pthread_mutex_t)
 */

 #ifndef TYPES_H
 #define TYPES_H
 
 #include <stdio.h>
 #include <stdint.h>
 #include <pthread.h>
 
 /**
  * @struct log_args
  * @brief Mensaje intercambiado entre Minero y Logger a través de la tubería.
  *
  * Contiene información necesaria para registrar una ronda:
  *  - round: número de ronda (Id).
  *  - target: objetivo buscado en la ronda.
  *  - solution: solución encontrada (s).
  *  - valid: 1 si se considera validada, 0 si se marca como rechazada (en esta iteración
  *           puede forzarse para pruebas según el enunciado).
  */
 typedef struct _log_args{
     int round;          /** Número de ronda */
     uint32_t target;    /** Objetivo de la ronda */
     uint32_t solution;  /** Solución encontrada */
     int valid;          /** 1 = validated, 0 = rejected */
 } log_args;
 
 /**
  * @struct worker_args
  * @brief Argumentos de trabajo para un hilo del Minero.
  *
  * Cada hilo explora un subrango del espacio [0, POW_LIMIT):
  *  - start, end: límites del subrango asignado al hilo.
  *  - target: objetivo actual de la ronda.
  *
  * Sincronización / parada temprana:
  *  - solution apunta a una variable compartida donde se escribirá la solución ganadora.
  *  - found apunta a un flag compartido que indica si ya existe solución (parada temprana).
  *
  * @note El campo mutex puede estar sin uso si se emplean operaciones atómicas en found.
  *       Se mantiene para compatibilidad/claridad con versiones previas.
  */
 typedef struct _worker_args{
     uint32_t start;     /** Inicio del subrango (incluido) */
     uint32_t end;       /** Fin del subrango (excluido) */
     uint32_t target;    /** Target de la ronda */
 
     uint32_t *solution; /** Puntero a la solución compartida */
     int *found;         /** Flag compartido: 1 si ya se encontró solución */
 
     pthread_mutex_t *mutex; /** Mutex opcional (puede no usarse si se emplean atómicos) */
     int round;              /** Número de ronda (para trazas) */
     int write_fd;           /** FD de escritura al logger (si el hilo escribe) */
 } worker_args;
 
 #endif /* TYPES_H */