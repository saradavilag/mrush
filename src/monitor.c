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
 * Se encarga de destruir todos los semáforos alojados en la memoria compartida,
 * desmapear el segmento de memoria, eliminar el objeto SHM del sistema y 
 * cerrar/eliminar la cola de mensajes POSIX.[cite: 15]
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
        sem_destroy(&shm->sem_inscripcion);

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
 * Crea un segmento de memoria compartida con el tamaño de SharedData, lo mapea
 * en el espacio de direcciones, inicializa todos los semáforos anónimos como 
 * compartidos entre procesos (pshared=1) y crea la cola de mensajes con los 
 * atributos exigidos.[cite: 15]
 * 
 * @param shm_ptr Dirección del puntero a la estructura SharedData (salida).
 * @param mq Puntero al descriptor de la cola de mensajes (salida).
 * @return int EXIT_SUCCESS en éxito, EXIT_FAILURE si falla algún recurso.
 * 
 * @note Limpia la SHM con memset e inicializa target a 0.[cite: 15]
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
    sem_init(&shm->sem_inscripcion, 1, 1);

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

/**
 * @brief Punto de entrada del ejecutable monitor.
 * 
 * Gestiona los argumentos, inicializa los recursos globales del sistema e
 * inicia la jerarquía de procesos: el padre ejecuta el Comprobador y el
 * hijo el Monitor. Al finalizar, sincroniza la terminación y limpia el sistema.[cite: 15]
 * 
 * @param argc Número de argumentos.
 * @param argv Array de argumentos (se esperan LAG_COMPROBADOR y LAG_MONITOR).
 * @return int EXIT_SUCCESS o EXIT_FAILURE.
 */
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

/**
 * @brief Bucle principal del proceso Comprobador.
 * 
 * Recibe mensajes de los mineros a través de la cola de mensajes, valida
 * la solución propuesta, actualiza la información global (carteras, nuevo objetivo)
 * y deposita bloques en el buffer circular siguiendo el modelo productor-consumidor.
 * Gestiona las barreras de sincronización para que los Loggers y Mineros puedan 
 * procesar los resultados de la ronda antes de pasar a la siguiente.[cite: 15]
 * 
 * @param shm Puntero a la memoria compartida.
 * @param mq Descriptor de la cola de mensajes.
 * @param lag_comp Tiempo de retardo en milisegundos por iteración.
 */
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
            sem_post(&shm->sem_inscripcion);
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

/**
 * @brief Bucle principal del proceso Monitor.
 * 
 * Actúa como consumidor del buffer circular alojado en memoria compartida.
 * Extrae los bloques depositados por el Comprobador y muestra por salida
 * estándar si la solución fue aceptada o rechazada con el formato especificado.[cite: 15]
 * 
 * @param shm Puntero a la memoria compartida.
 * @param lag_monitor Tiempo de retardo en milisegundos por iteración.
 */
void monitor_run(SharedData *shm, int lag_monitor) {
    InfoBlock block;

    printf("[%d] Printing blocks ...\n", getpid());
    fflush(stdout);

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

    printf("[%d] Finishing\n", getpid());
    fflush(stdout);
}