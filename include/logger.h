/**
 * @file logger.h
 * @author Sara, Marco
 * @date 2026-02-27
 * @brief Interfaz del proceso Registrador (Logger) del proyecto Miner Rush.
 *
 * Este módulo define la función principal del proceso Registrador, que recibe
 * información del proceso Minero a través de una tubería (pipe) y genera un fichero
 * de log con formato de bloque.
 *
 * Dependencias:
 *  - types.h: define la estructura log_args que viaja por la tubería.
 *  - sys/stat.h, fcntl.h, unistd.h: creación de directorio/archivo y E/S POSIX.
 */

 #ifndef LOGGER_H
 #define LOGGER_H
 
 #include <stdio.h>      /* perror, fprintf (en caso de depuración) */
 #include <stdlib.h>     /* EXIT_SUCCESS, EXIT_FAILURE */
 #include <stdint.h>     /* uint32_t */
 #include <unistd.h>     /* read, close, getppid */
 #include <fcntl.h>      /* open */
 #include <sys/types.h>  /* pid_t */
 #include <sys/stat.h>   /* mkdir */
 #include <errno.h>      /* errno */
 
 #include "types.h"      /* log_args */
 
 /**
  * @brief Ejecuta el proceso Registrador.
  *
  * El Registrador lee mensajes del tipo log_args desde la tubería conectada con el Minero
  * y los vuelca en un fichero "log/<ppid>.log", donde <ppid> es el PID del proceso padre.
  *
  * El proceso termina cuando detecta EOF en read_fd (el Minero cierra su extremo de escritura)
  * o cuando recibe un mensaje de finalización si se implementa dicha señal.
  *
  * @param read_fd Descriptor de fichero para leer mensajes enviados por el Minero.
  * @param write_fd Descriptor de fichero para enviar ACK al Minero (no siempre usado).
  * @return EXIT_SUCCESS si termina correctamente, EXIT_FAILURE si ocurre un error.
  *
  * @note Si el diseño no usa ACK, write_fd puede ignorarse dentro de la implementación.
  */
 int logger_run(int read_fd, int write_fd);
 
 #endif /* LOGGER_H */