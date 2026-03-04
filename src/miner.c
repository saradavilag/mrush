/**
 * @file miner.c
 * @author Sara, Marco
 * @date 2026-03-04
 * @brief Implementación del proceso Minero multihilo (Miner Rush - ejercicio 13b).
 *
 * El minero resuelve una prueba de esfuerzo (POW) por fuerza bruta. Para cada ronda:
 *  - Divide el rango [0, POW_LIMIT) en subrangos para N_THREADS hilos.
 *  - Lanza hilos en paralelo que buscan un valor s tal que pow_hash(s) == target.
 *  - En cuanto un hilo encuentra solución, se marca un flag compartido (found) para
 *    que el resto termine cuanto antes.
 *  - Se envía un mensaje log_args al Logger por una tubería.
 *
 * Decisiones de diseño relevantes:
 *  - Sincronización sin mutex: found se consulta/actualiza con operaciones atómicas GCC
 *    (__atomic_*). Evita la contención de un mutex por iteración y mejora el rendimiento.
 *  - write_all(): write() puede escribir menos bytes que los solicitados; se asegura el envío
 *    completo de sizeof(log_args) al Logger.
 *
 * Dependencias:
 *  - pow.h: pow_hash() y POW_LIMIT.
 *  - pthread: creación y sincronización de hilos.
 *  - types.h: estructuras compartidas worker_args y log_args.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <unistd.h>
 #include <errno.h>
 #include <string.h>
 
 #include "miner.h"
 #include "types.h"
 #include "pow.h"
 
 /**
  * @brief Función de trabajo de cada hilo minero.
  *
  * Cada hilo explora el rango [start, end) buscando un valor i tal que pow_hash(i) == target.
  * Consulta el flag compartido found para terminar pronto si otro hilo ya encontró solución.
  *
  * @param arg Puntero a worker_args con el rango y punteros compartidos.
  * @return NULL 
  */
 static void *worker(void *arg) {
     worker_args *a = (worker_args *)arg;
 
     /* Campos que pueden no usarse en esta versión (compatibilidad con types.h) */
     (void)a->mutex;
     (void)a->round;
     (void)a->write_fd;
 
     for (uint32_t i = a->start; i < a->end; i++) {
 
         /* Lectura atómica: si ya existe solución, salimos */
         int already = __atomic_load_n(a->found, __ATOMIC_ACQUIRE);
         if (already) break;
 
         /* pow_hash trabaja con long int */
         long int h = pow_hash((long int)i);
 
         if (h == (long int)a->target) {
             /* Intentar ser el ganador:
                compare_exchange asegura que solo un hilo cambia found de 0 a 1. */
             int expected = 0;
             if (__atomic_compare_exchange_n(
                     a->found, &expected, 1,
                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                 /* Este hilo ha sido el primero: publica la solución */
                 *(a->solution) = i;
             }
             break;
         }
     }
 
     return NULL;
 }
 
 /**
  * @brief Escribe exactamente n bytes en un descriptor.
  *
  * write() puede devolver escrituras parciales; esta función reintenta hasta escribir todo
  * o devolver error.
  *
  * @param fd Descriptor de fichero destino.
  * @param buf Buffer origen.
  * @param n Número de bytes a escribir.
  * @return 0 si OK, -1 si error (errno se preserva).
  */
 static int write_all(int fd, const void *buf, size_t n) {
     const char *p = (const char *)buf;
     size_t left = n;
 
     while (left > 0) {
         ssize_t w = write(fd, p, left);
         if (w < 0) {
             if (errno == EINTR) continue; /* reintentar si se interrumpe */
             return -1;
         }
         p += (size_t)w;
         left -= (size_t)w;
     }
     return 0;
 }
 
 /**
  * @brief Ejecuta el minado durante un número de rondas con N_THREADS hilos.
  *
  * @param write_fd FD para enviar mensajes log_args al Logger.
  * @param read_fd  FD para leer ACK del Logger (no usado en esta versión).
  * @param target_ini Target inicial de la ronda 1.
  * @param rounds Número de rondas a minar.
  * @param n_threads Número de hilos por ronda.
  * @return EXIT_SUCCESS si todo correcto, EXIT_FAILURE si ocurre algún error.
  */
 int miner_run(int write_fd, int read_fd, uint32_t target_ini, int rounds, int n_threads) {
     (void)read_fd; // No usamos ACK (por si futuras prácticas).
 
     /* Validación mínima de parámetros */
     if (rounds <= 0 || n_threads <= 0) {
         fprintf(stderr, "miner_run: rounds and n_threads must be > 0\n");
         return EXIT_FAILURE;
     }
 
     uint32_t target = target_ini;
 
     pthread_t *tids = calloc((size_t)n_threads, sizeof(*tids));
     worker_args *args = calloc((size_t)n_threads, sizeof(*args));
     if (!tids || !args) {
         perror("calloc");
         free(tids);
         free(args);
         return EXIT_FAILURE;
     }
 
     for (int round = 1; round <= rounds; round++) {
 
         uint32_t solution = 0;
         int found = 0;
 
         /* Particionamos [0, POW_LIMIT) en n_threads trozos:
            - base: tamaño mínimo por hilo
            - rem: los primeros "rem" hilos reciben 1 elemento extra (reparto equilibrado) */
         uint32_t base = (uint32_t)POW_LIMIT / (uint32_t)n_threads;
         uint32_t rem  = (uint32_t)POW_LIMIT % (uint32_t)n_threads;
 
         uint32_t start = 0;
 
         for (int k = 0; k < n_threads; k++) {
             uint32_t chunk = base + ((uint32_t)k < rem ? 1u : 0u);
             uint32_t end = start + chunk;
 
             args[k].start = start;
             args[k].end = end;
             args[k].target = target;
 
             args[k].solution = &solution;
             args[k].found = &found;
 
             /* mutex no usado, mantenido por compatibilidad con worker_args */
             args[k].mutex = NULL;
 
             /* campos auxiliares no usados en worker en esta versión */
             args[k].round = round;
             args[k].write_fd = write_fd;
 
             int err = pthread_create(&tids[k], NULL, worker, &args[k]);
             if (err != 0) {
                 fprintf(stderr, "pthread_create: %s\n", strerror(err));
                 /* esperar a los hilos ya creados para limpiar */
                 for (int j = 0; j < k; j++) pthread_join(tids[j], NULL);
                 free(tids);
                 free(args);
                 return EXIT_FAILURE;
             }
 
             start = end;
         }
 
         /* Esperar a todos los hilos */
         for (int k = 0; k < n_threads; k++) {
             int err = pthread_join(tids[k], NULL);
             if (err != 0) {
                 fprintf(stderr, "pthread_join: %s\n", strerror(err));
                 free(tids);
                 free(args);
                 return EXIT_FAILURE;
             }
         }
 
         /* Si no se encontró solución, se informa (segun pow_hash debería existir una) */
         if (!__atomic_load_n(&found, __ATOMIC_ACQUIRE)) {
             fprintf(stderr, "Round %d: no solution found for target %08u\n", round, target);
         }
 
         /* Validación: por defecto accepted/validated (puede forzarse rejected según enunciado) */
         int valid = 1;
         if (valid) {
             printf("Solution accepted : %08u --> %08u\n", target, solution);
         } else {
             printf("Solution rejected : %08u --> %08u\n", target, solution);
         }
 
         /* Construir mensaje y enviarlo al logger */
         log_args msg;
         msg.round = round;
         msg.target = target;
         msg.solution = solution;
         msg.valid = valid;
 
         if (write_all(write_fd, &msg, sizeof(msg)) != 0) {
             perror("write to logger pipe");
             free(tids);
             free(args);
             return EXIT_FAILURE;
         }
 
         /* La solución de esta ronda se convierte en el target de la siguiente */
         target = solution;
     }
 
     free(tids);
     free(args);
     return EXIT_SUCCESS;
 }