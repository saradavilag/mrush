/**
 * @file monitor.c
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Implementación de los procesos Comprobador y Monitor (Miner Rush - P3).
 *
 * El Comprobador (padre) lee de la cola de mensajes, valida soluciones, actualiza 
 * la memoria compartida (carteras, objetivo) y actúa como Productor. 
 * El Monitor (hijo) actúa como Consumidor extrayendo del buffer e imprimiendo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "monitor.h"
#include "pow.h"

/* Definiciones por si no están en monitor.h */
#ifndef SHM_NAME
#define SHM_NAME "/shm_miner_rush"
#endif

#ifndef MQ_NAME
#define MQ_NAME "/mq_miner_rush"
#endif

#define MQ_MAX_MSG 7 // Exigido por el enunciado (Apartado c)

void comprobador_run(SharedData *shm, mqd_t mq, int lag_comp);
void monitor_run(SharedData *shm, int lag_mon);

/**
 * @brief Libera y elimina los recursos globales del sistema (SHM, MQ y semáforos).
 * 
 * @param shm Puntero a la estructura de memoria compartida a liberar.
 * @param mq Descriptor de la cola de mensajes a cerrar y eliminar.
 */
void clean_system(SharedData *shm, mqd_t mq) {
    /* Destruimos la memoria compartida si existe */
    if (shm != NULL) {
        sem_destroy(&shm->sem_mutex_buffer);
        sem_destroy(&shm->sem_empty);
        sem_destroy(&shm->sem_fill);
        sem_destroy(&shm->sem_mutex_red);
        sem_destroy(&shm->sem_votes);
        sem_destroy(&shm->sem_mutex_votes);
        sem_destroy(&shm->sem_data_act);
        sem_destroy(&shm->sem_loggers_printed);
        sem_destroy(&shm->sem_ready_next_round);

        /* La desmapeamos también */
        if (munmap(shm, sizeof(SharedData)) == -1) {
            perror("munmap");
        }
    }

    shm_unlink(SHM_NAME);

    /* Cerramos y destruimos la cola de mensajes también si existiera */
    if (mq != (mqd_t)-1) {
        mq_close(mq);
    }

    mq_unlink(MQ_NAME);
}

/**
 * @brief Inicializa la memoria compartida (SHM), semáforos y cola de mensajes (MQ).
 * 
 * @param shm_ptr Dirección del puntero a la estructura SharedData (salida).
 * @param mq Puntero al descriptor de la cola de mensajes (salida).
 * @return int EXIT_SUCCESS en éxito, EXIT_FAILURE si falla algún recurso.
 * 
 * @note Limpia la SHM con memset, pone target a 0 e inicializa semáforos entre procesos.
 */
int init_system(SharedData **shm_ptr, mqd_t *mq){
    if (!shm_ptr) return EXIT_FAILURE;

    /* Creamos la memoria compartida */
    shm_unlink(SHM_NAME);
    int fd_shm = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd_shm == -1) { 
        perror("shm_open"); 
        return EXIT_FAILURE; 
    }

    if (ftruncate(fd_shm, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        close(fd_shm);
        return EXIT_FAILURE;
    }

    *shm_ptr = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (*shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(fd_shm);
        return EXIT_FAILURE;
    } 

    close(fd_shm);

    /* Usamos un puntero local para que sea más legible */
    SharedData *shm = *shm_ptr;
    memset(shm, 0, sizeof(SharedData));
    
    /* Inicializamos los semáforos */
    sem_init(&shm->sem_mutex_buffer, 1, 1);
    sem_init(&shm->sem_empty, 1, BUFFER_CIRCULAR_SIZE);
    sem_init(&shm->sem_fill, 1, 0);
    sem_init(&shm->sem_mutex_red, 1, 1);
    sem_init(&shm->sem_votes, 1, 0);
    sem_init(&shm->sem_mutex_votes, 1, 1);
    sem_init(&shm->sem_ready_next_round, 1, 0);
    sem_init(&shm->sem_loggers_printed, 1, 0);
    sem_init(&shm->sem_data_act, 1, 0);

    /* Inicializamos la primera target */
    shm->target = 0; 

    /* Creamos la cola de mensajes */
    mq_unlink(MQ_NAME);
    struct mq_attr attr = {0};
    attr.mq_maxmsg = MQ_MAX_MSG;
    attr.mq_msgsize = sizeof(Message);
    
    *mq = mq_open(MQ_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, &attr);
    if (*mq == (mqd_t)-1) { 
        perror("mq_open"); 
        clean_system(shm, *mq);
        return EXIT_FAILURE; 
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    /* Comprobación de argumentos */
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <LAG_COMPROBADOR_MS> <LAG_MONITOR_MS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Obtención de argumentos */
    int lag_comp = atoi(argv[1]);
    int lag_mon = atoi(argv[2]);

    /* Inicialización */
    SharedData *shm = NULL;
    mqd_t mq;

    if (init_system(&shm, &mq) == EXIT_FAILURE){
        clean_system(shm, mq);
        return EXIT_FAILURE;
    }

    /* Bifurcamos el proceso monitor y comprobador*/
    pid_t pid = fork();
    if (pid == 0) {
        /* Proceso Hijo: Monitor (Consumidor) */
        monitor_run(shm, lag_mon);
        exit(EXIT_SUCCESS);
    } else {
        /* Proceso Padre: Comprobador (Productor) */
        comprobador_run(shm, mq, lag_comp);
        wait(NULL); 

        /* Limpiamos la memoria compartida y mensajes */
        clean_system(shm, mq);
    }
    return EXIT_SUCCESS;
}

void comprobador_run(SharedData *shm, mqd_t mq, int lag_comp) {
    Message msg;
    InfoBlock block;
    int i;

    /* Empieza el bucle */
    while (1) {
        /* Recibimos el mensaje de la cola de mensajes */
        if (mq_receive(mq, (char *)&msg, sizeof(Message), NULL) == -1) {
            perror("mq_receive");
            break;
        }

        /* DEBUG */
        if (msg.is_last_miner) printf("Procesado mensaje is_last_miner");

        /* Preparar el bloque y validarlo */
        block.target = msg.target;
        block.solution = msg.solution;
        block.is_final = msg.is_last_miner;

        if (!block.is_final) {
            sem_wait(&shm->sem_mutex_red);

            /* Comprobación de que no es un mensaje atrasado de una ronda ya ganada */
            if (msg.target != shm->target) {
                block.validated = 0;
            } else {
                long int hash_result = pow_hash((long int)msg.solution);
                block.validated = (hash_result == (long int)msg.target);
            }

            shm->solution = 0;
            shm->hay_ganador = 0;
            shm->target = block.solution;
            shm->validation_result = block.validated;

            if (block.validated) {
                /* Buscamos al ganador para sumarle la moneda */
                for (int i = 0; i < MAX_MINERS_GLOBAL; i++) {
                    if (shm->active_miners[i] == msg.miner_pid) {
                        shm->wallets[i]++;
                        break;
                    }
                }
            }

            sem_post(&shm->sem_mutex_red);
        } else {
            block.validated = 0; 
        }

        /* 3. Sección Crítica: Productor (Añadir al buffer) */
        sem_wait(&shm->sem_empty); 
        sem_wait(&shm->sem_mutex_buffer); 
        
        shm->buffer[shm->head] = block;
        shm->head = (shm->head + 1) % BUFFER_CIRCULAR_SIZE;
        
        sem_post(&shm->sem_mutex_buffer); 
        sem_post(&shm->sem_fill);  

        sem_wait(&shm->sem_mutex_red);
        int mineros_ronda = shm->num_active_miners;
        shm->loggers_finished = mineros_ronda; 
        sem_post(&shm->sem_mutex_red);

        if (!block.is_final && mineros_ronda > 0) {
            for (i = 0; i < mineros_ronda; i++) {
                sem_post(&shm->sem_data_act); 
            }
            sem_wait(&shm->sem_loggers_printed); 

            for (i = 0; i < mineros_ronda; i++) {
                sem_post(&shm->sem_ready_next_round);
            }
        }
        
        /* 4. Condición de salida */
        if (block.is_final) {
            break; 
        }

        /* Lag */
        usleep(lag_comp * 1000);
    }
}

void monitor_run(SharedData *shm, int lag_monitor) {
    InfoBlock block;

    while (1) {
        /* Abrimos el buffer*/
        sem_wait(&shm->sem_fill);
        sem_wait(&shm->sem_mutex_buffer); 
        
        block = shm->buffer[shm->tail];
        shm->tail = (shm->tail + 1) % BUFFER_CIRCULAR_SIZE;
        
        sem_post(&shm->sem_mutex_buffer); 
        sem_post(&shm->sem_empty); 

        /* 2. Condición de finalización */
        if (block.is_final) {
            break; 
        }

        /* 3. Mostrar por pantalla */
        if (block.validated) {
            printf("Solution accepted: %08ld --> %08ld\n", 
                  (long int)block.target, (long int)block.solution);
        } else {
            printf("Solution rejected: %08ld !-> %08ld\n", 
                  (long int)block.target, (long int)block.solution);
        }

        /* Lag */
        usleep(lag_monitor * 1000);
    }
}