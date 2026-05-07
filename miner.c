/**
 * @file miner.c
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Implementación del proceso Minero multihilo adaptado a Memoria Compartida (Miner Rush - Práctica 3).
 *
 * El minero resuelve una prueba de esfuerzo (POW) por fuerza bruta,
 * coordinándose con una red global a través de memoria compartida y colas de mensajes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <mqueue.h>
#include <semaphore.h>
#include <pthread.h>

#include "miner.h"
#include "types.h"
#include "pow.h"
#include "monitor.h"

/* Variables globales manejadas en main.c */
extern volatile sig_atomic_t got_sig_exit;
extern volatile sig_atomic_t got_sig_usr2;
extern sigset_t wait_mask_usr1;
extern sigset_t wait_mask_usr2;

/**
 * @brief Función que envía señales a los demás mineros
 *
 * @param sig Señal a enviar
 * @return Número de señales enviadas
 */
int send_sig_to_miners(SharedData *shm, int sig){

    sem_wait(&shm->sem_mutex_red);

    int active_count = 0;
    for(int i = 0; i < MAX_MINERS_GLOBAL; i++){
        if(shm->active_miners[i] != 0 && shm->active_miners[i] != getpid()) {
            kill(shm->active_miners[i], sig);
            active_count++;
        }
    }

    sem_post(&shm->sem_mutex_red);

    return active_count;
}


/**
 * @brief Función de trabajo de cada hilo minero.
 *
 * @param arg Puntero a worker_args con el rango y punteros compartidos.
 * @return NULL 
 */
static void *worker(void *arg) {
    worker_args *a = (worker_args *)arg;

    for (uint32_t i = a->start; i < a->end; i++) {
        int already = __atomic_load_n(a->found, __ATOMIC_ACQUIRE);
        if (already || got_sig_usr2 || got_sig_exit) break;

        long int h = pow_hash((long int)i);

        if (h == (long int)a->target) {
            int expected = 0;
            if (__atomic_compare_exchange_n(a->found, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
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
 * @param fd Descriptor de fichero destino (tubería).
 * @param buf Buffer origen.
 * @param n Número de bytes a escribir.
 * @return 0 si OK, EXIT_FAILURE si error (errno se preserva).
 */
static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;

    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return EXIT_FAILURE;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

/**
 * @brief Cuenta el número de mineros actualmente activos en la red.
 * 
 * @param shm Puntero a la estructura de Memoria Compartida.
 * @return int El número de mineros activos contados.
 */
int count_active_miners(SharedData *shm) {
    int count = 0;

    if (!shm) return 0;

    /* Protegemos la lectura del censo global */
    sem_wait(&shm->sem_mutex_red);

    /* Recorremos el array de mineros activos */
    for (int i = 0; i < MAX_MINERS_GLOBAL; i++) {
        if (shm->active_miners[i] != 0) {
            count++;
        }
    }

    sem_post(&shm->sem_mutex_red);

    return count;
}

static void barrier_exit(SharedData *shm) {
    sem_wait(&shm->sem_mutex_red);
    shm->loggers_finished--;
    if (shm->loggers_finished == 0) {
        sem_post(&shm->sem_loggers_printed);
    }
    sem_post(&shm->sem_mutex_red);
}

/**
 * @brief Ejecuta el ciclo de minado principal integrado con la red global.
 *
 * @param write_fd Descriptor de la tubería para enviar datos al Logger local.
 * @param shm Puntero al bloque de memoria compartida (SharedData).
 * @param mq Descriptor de la cola de mensajes hacia el Comprobador.
 * @param mi_indice Posición del minero en active_miners[].
 * @param n_threads Número de hilos lanzados por ronda de minado.
 * @return EXIT_SUCCESS si finaliza correctamente, EXIT_FAILURE ante error.
 */
int miner_run(int write_fd, SharedData *shm, mqd_t mq, int mi_indice, int n_threads) {
    if (n_threads <= 0) return EXIT_FAILURE;

    pthread_t *tids = calloc((size_t)n_threads, sizeof(*tids));
    worker_args *args = calloc((size_t)n_threads, sizeof(*args));
    
    if (!tids || !args) {
        free(tids);
        free(args);
        return EXIT_FAILURE;
    }

    int round = 1;
    int i;

    /* Bucle infinito controlado por señales */
    while (!got_sig_exit) {
      
        /* 1. Lectura del objetivo actual (Protegida con mutex de red) */
        sem_wait(&shm->sem_mutex_red);
        uint32_t current_target = shm->target;
        sem_post(&shm->sem_mutex_red);

        uint32_t solution = 0;
        int found = 0;
        uint32_t solution_this_round = 0;
        
        uint32_t base = (uint32_t)POW_LIMIT / (uint32_t)n_threads;
        uint32_t rem  = (uint32_t)POW_LIMIT % (uint32_t)n_threads;
        uint32_t start = 0;
        got_sig_usr2 = 0;

        /* 2. Reparto de trabajo y lanzamiento de hilos */
        for (int k = 0; k < n_threads; k++) {
            uint32_t chunk = base + ((uint32_t)k < rem ? 1u : 0u);
            args[k].start = start;
            args[k].end = start + chunk;
            args[k].target = current_target;
            args[k].solution = &solution;
            args[k].found = &found;
            
            pthread_create(&tids[k], NULL, worker, &args[k]);
            start += chunk;
        }

        /* 3. Espera de hilos */
        for (int k = 0; k < n_threads; k++) {
            pthread_join(tids[k], NULL);
        }

        /* Si la señal llegó mientras minábamos, salimos sin entrar al protocolo */
        if (got_sig_exit) break;

        /* Actualización de memoria compartida */
        sem_wait(&shm->sem_mutex_red);
    
        if (found && !shm->hay_ganador) {
            /* Ganador */
            /* Corrompemos una solución de forma forzada para simular rechazo */
            int valid_forced = (round % 5 != 0);
            if (!valid_forced && found) solution += 1;

            shm->hay_ganador = 1;
            shm->solution = solution;
            shm->solution_winner = getpid();
            solution_this_round = solution;

            if (pow_hash(shm->solution) == shm->target){
                shm->votes_yes = 1;
                shm->votes_no = 0;
            }
            else{
                shm->votes_yes = 0;
                shm->votes_no = 1;
            }

            sem_post(&shm->sem_mutex_red);

            /* Emviamos la señal para votar */
            int active_count = 0;
            active_count = send_sig_to_miners(shm, SIGUSR2);
        
            /* Esperamos a que todos voten */
            for (i = 0; i < active_count; i++){
                sem_wait(&shm->sem_votes);
            }

            /* Evaluamos si enviar o no al comprobador */
            sem_wait(&shm->sem_mutex_votes);
            int total = active_count + 1;
            int won_vote = (shm->votes_yes > total / 2);

            /* --- IMPRESIÓN POR PANTALLA DEL GANADOR --- */
            printf("Winner %d => [ ", getpid());
            
            /* Imprimimos las 'Y' de los votantes */
            for (int v = 0; v < shm->votes_yes; v++) {
                printf("Y ");
            }
            /* Imprimimos las 'N' de los votantes */
            for (int v = 0; v < shm->votes_no; v++) {
                printf("N ");
            }
            
            /* Imprimimos Accepted/Rejected según el resultado de LA VOTACIÓN */
            printf("] => %s\n", won_vote ? "Accepted" : "Rejected");
            fflush(stdout); 

            sem_post(&shm->sem_mutex_votes);

            /* Si la red ha aceptado la solución, se la mandamos al Comprobador */
            /* SIEMPRE ENVIAMOS AL COMPROBADOR (Para no romper la barrera) */
            Message mq_msg = {0};
            mq_msg.target = current_target;
            mq_msg.solution = solution;
            mq_msg.miner_pid = getpid();
            mq_msg.is_last_miner = 0;
            mq_send(mq, (char *)&mq_msg, sizeof(Message), 1);
            
    

        } else {
            sem_post(&shm->sem_mutex_red);

            sigsuspend(&wait_mask_usr2);

            if (got_sig_exit) {
                /* Liberamos el voto para no bloquear al ganador */
                sem_post(&shm->sem_votes);
                break;
            }

            /* Guardamos la solución antes de votar */
            solution_this_round = shm->solution;

            /* Votamos */
            sem_wait(&shm->sem_mutex_votes);
            
            /* Comprobar el hash de la solución propuesta */
            long int hash_result = pow_hash((long int)shm->solution);
            
            if (hash_result == (long int)shm->target) {
                shm->votes_yes++;
            } else {
                shm->votes_no++;
            }
            
            sem_post(&shm->sem_mutex_votes);
            sem_post(&shm->sem_votes);
        }

        /* ---- ZONA DE LA BARRERA ---- */
        if (got_sig_exit) {
            barrier_exit(shm);
            break;
        }

        sem_wait(&shm->sem_data_act);

        if (got_sig_exit) {
            barrier_exit(shm);
            break;
        }

        log_args log_msg = {0};
        sem_wait(&shm->sem_mutex_red);
        log_msg.round = round;
        log_msg.winner_pid = shm->solution_winner; 
        log_msg.target = current_target;
        log_msg.solution = solution_this_round;
        log_msg.valid = shm->validation_result;       
        log_msg.votes_yes = shm->votes_yes; 
        log_msg.total_votes = shm->num_active_miners;
        log_msg.coins = shm->wallets[mi_indice];
        sem_post(&shm->sem_mutex_red);

        

        if (write_all(write_fd, &log_msg, sizeof(log_msg)) != 0) {
            break;
        }

        /* --- BARRERA: CUENTA ATRÁS --- */
        sem_wait(&shm->sem_mutex_red);
        shm->loggers_finished--;
        if (shm->loggers_finished == 0) {
             sem_post(&shm->sem_loggers_printed);
        }
        sem_post(&shm->sem_mutex_red);

        /* Esperamos el permiso para la siguiente ronda,
           pero si ya tenemos la señal de salida, no nos bloqueamos */
        if (!got_sig_exit) {
            sem_wait(&shm->sem_ready_next_round);
        }

        /* El minero ganador avisa a los demás SIEMPRE antes de salir */
        sem_wait(&shm->sem_mutex_red);
        if (shm->solution_winner == getpid()) {
            sem_post(&shm->sem_mutex_red);
            send_sig_to_miners(shm, SIGUSR1);
        } else {
            sem_post(&shm->sem_mutex_red);
            /* Los votantes esperan (salvo que ya tengan que irse) */
            if (!got_sig_exit) sigsuspend(&wait_mask_usr1);
        }

        /* ¡AHORA SÍ! Si se acabó nuestro tiempo, salimos */
        if (got_sig_exit) break; 
        
        round++;
    }

    free(tids);
    free(args);
    return EXIT_SUCCESS;
}

/**
 * @brief Registra al minero en la red de memoria compartida.
 *
 * @param shm Puntero al bloque de memoria compartida.
 * @return El índice asignado en la red (>=0), o -1 si la red está llena.
 */
int miner_add_system(SharedData *shm) {
    int index = -1;
    if (!shm) return -1;

    sem_wait(&shm->sem_mutex_red); // Mutex de la red
    for (int i = 0; i < MAX_MINERS_GLOBAL; i++) {
        if (shm->active_miners[i] == 0) {
            shm->active_miners[i] = getpid();
            if (shm->num_active_miners == 0 && shm->target == 0) shm->solution_winner = getpid();
            shm->num_active_miners++;
            index = i;
            break;
        }
    }
    sem_post(&shm->sem_mutex_red);

    return index;
}

/**
 * @brief Elimina al minero de la red de memoria compartida y gestiona el cierre.
 *
 * @param shm Puntero al bloque de memoria compartida.
 * @param mq Descriptor de la cola de mensajes hacia el Comprobador.
 * @param mi_indice El índice asignado al minero durante el registro.
 * @return EXIT_SUCCESS si se eliminó correctamente, EXIT_FAILURE si hay error.
 */
int miner_del_system(SharedData *shm, mqd_t mq, int mi_indice) {
    if (!shm || mi_indice < 0 || mi_indice >= MAX_MINERS_GLOBAL) return EXIT_FAILURE;

    sem_wait(&shm->sem_mutex_red); // Mutex de la red
    shm->active_miners[mi_indice] = 0;
    shm->num_active_miners--;
    int is_last = (shm->num_active_miners == 0);
    sem_post(&shm->sem_mutex_red);

    if (is_last) {
        Message bye_msg = {0};
        bye_msg.is_last_miner = 1;
        mq_send(mq, (char *)&bye_msg, sizeof(Message), 1);
    }

    return EXIT_SUCCESS;
}