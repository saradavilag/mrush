/**
 * @file logger.h
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Interfaz del proceso Registrador (Logger) del proyecto Miner Rush.
 *
 * Este módulo define la función principal del proceso Registrador, que recibe
 * información validada de la red a través de una tubería (pipe) y genera un fichero
 * de log con formato de bloque.
 */

#ifndef LOGGER_H
#define LOGGER_H

/**
 * @brief Ejecuta el proceso Registrador.
 *
 * El Registrador lee mensajes del tipo log_args desde la tubería conectada con el Minero
 * y los vuelca en un fichero "log/<ppid>.log", donde <ppid> es el PID del proceso padre.
 *
 * El proceso termina cuando detecta EOF en read_fd (el Minero cierra su extremo de escritura).
 *
 * @param read_fd Descriptor de fichero para leer mensajes enviados por el Minero.
 * @param write_fd Descriptor de fichero para enviar ACK al Minero (ignoramos en P3).
 * @return EXIT_SUCCESS si termina correctamente, EXIT_FAILURE si ocurre un error.
 */
int logger_run(int read_fd, int write_fd);

#endif /* LOGGER_H */