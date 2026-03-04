/**
 * @file logger.c
 * @author Sara, Marco
 * @date 2026-03-04
 * @brief Implementación del proceso Registrador (Logger) para Miner Rush.
 *
 * El Logger lee mensajes (log_args) enviados por el proceso Minero mediante una tubería
 * (pipe) y los vuelca en un fichero de log "log/<ppid>.log", donde <ppid> es el PID del
 * proceso padre (Miner).
 *
 * Decisiones de diseño relevantes:
 *  - Lecturas parciales en pipes: read() no garantiza devolver sizeof(log_args) bytes de una sola vez.
 *    Se implementa read_all() para reconstruir un mensaje completo de tamaño fijo.
 *  - ACK deshabilitado: en esta versión no se envían ACKs para evitar SIGPIPE si el receptor
 *    no consume (o se cierra) el extremo correspondiente.
 *
 * Dependencias / interfaces:
 *  - logger.h: prototipo de logger_run() y tipos compartidos (types.h -> log_args).
 *  - unistd.h, fcntl.h, sys/stat.h: E/S POSIX y creación de directorio.
 */

 #include "logger.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <errno.h>
 #include <string.h>
 
 /**
  * @brief Lee exactamente n bytes de un descriptor de fichero.
  *
  * En pipes, read() puede devolver menos bytes de los solicitados aunque no sea EOF.
  * Esta función repite lecturas hasta completar n bytes o detectar EOF/error.
  *
  * @param fd Descriptor de fichero desde el que leer.
  * @param buf Buffer destino.
  * @param n Número de bytes a leer.
  * @return  1 si se han leído n bytes correctamente,
  *          0 si se detecta EOF antes de completar n,
  *         -1 si ocurre un error (errno se preserva).
  */
 static int read_all(int fd, void *buf, size_t n) {
     char *p = (char *)buf;
     size_t left = n;
 
     while (left > 0) {
         ssize_t r = read(fd, p, left);
 
         if (r < 0) {
             if (errno == EINTR) continue; /* reintentar si señal interrumpe la syscall */
             return -1;                    /* error real */
         }
 
         if (r == 0) {
             return 0;                     /* EOF: el escritor ha cerrado la tubería */
         }
 
         p += (size_t)r;
         left -= (size_t)r;
     }
 
     return 1;                             /* leído completo */
 }
 
 /**
  * @brief Ejecuta el bucle principal del proceso Logger.
  *
  * Crea (si no existe) el directorio "log" y abre el fichero "log/<ppid>.log" en modo append.
  * Mientras la tubería permanezca abierta, lee mensajes log_args y escribe un bloque por ronda
  * usando dprintf() con el formato especificado en el enunciado.
  *
  * @param read_fd Descriptor desde el que se leen mensajes del Minero.
  * @param write_fd Descriptor para enviar ACKs al Minero (no usado en esta versión).
  * @return EXIT_SUCCESS si finaliza correctamente, EXIT_FAILURE ante cualquier error.
  */
 int logger_run(int read_fd, int write_fd) {
     (void)write_fd; // En esta versión no usamos ACK (evita SIGPIPE).
 
     char filename[128];
     int log_fd;
     pid_t parent_pid = getppid();
 
     /* Crear carpeta log si no existe. Si ya existe, mkdir devuelve -1 con EEXIST:
        lo ignoramos porque no es un error fatal para el funcionamiento. */
     mkdir("log", 0755);
 
     /* Nombre del fichero: log/<ppid>.log */
     snprintf(filename, sizeof(filename), "log/%d.log", parent_pid);
 
     /* Abrir en modo escritura, crear si no existe, y añadir al final */
     log_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
     if (log_fd < 0) {
         perror("open log file");
         return EXIT_FAILURE;
     }
 
     while (1) {
         log_args msg;
 
         /* Leer un mensaje completo. No usamos read() directo porque
            podría devolver lecturas parciales y romper la estructura msg. */
         int ok = read_all(read_fd, &msg, sizeof(msg));
         if (ok < 0) {
             perror("read pipe");
             close(log_fd);
             return EXIT_FAILURE;
         }
         if (ok == 0) {
             /* EOF: el minero cerró la tubería */
             break;
         }
 
         /* Mensaje de finalización */
         if (msg.round == -1)
             break;
 
         /* Escribir bloque en fichero con el formato indicado. */
         if (dprintf(log_fd,
             "Id : %d\n"
             "Winner : %d\n"
             "Target : %u\n"
             "Solution : %08u (%s)\n"
             "Votes : %d/%d\n"
             "Wallets : %d:%d\n\n",
             msg.round,
             parent_pid,
             msg.target,
             msg.solution,
             msg.valid ? "validated" : "rejected",
             msg.round,
             msg.round,
             parent_pid,
             msg.round) < 0)
         {
             perror("dprintf");
             close(log_fd);
             return EXIT_FAILURE;
         }
     }
 
     close(log_fd);
     close(read_fd);
     return EXIT_SUCCESS;
 }