/**
 * @file monitor.h
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Estructuras de Memoria Compartida y Colas de Mensajes (Práctica 3).
 *
 * Centraliza las definiciones necesarias para la comunicación entre la red de
 * Mineros y el sistema central (Comprobador/Monitor). Define la estructura 
 * principal alojada en memoria compartida (SharedData), los mensajes de la cola 
 * y los bloques del buffer circular.[cite: 16]
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <sys/types.h>
#include <semaphore.h>

/* Nombres unificados para los mecanismos IPC POSIX */
#define SHM_NAME "/shm_miner_rush" /**< Nombre del segmento de memoria compartida */
#define MQ_NAME  "/mq_miner_rush"  /**< Nombre de la cola de mensajes POSIX */

/* Constantes de dimensionamiento exigidas por el enunciado */
#define MAX_MINERS_GLOBAL 100      /**< Capacidad máxima de la red de mineros[cite: 16] */
#define BUFFER_CIRCULAR_SIZE 6     /**< Tamaño del buffer Comprobador-Monitor[cite: 16] */
#define MQ_MAX_MSG 7               /**< Capacidad máxima de la cola de mensajes[cite: 16] */

/**
 * @struct Message
 * @brief Estructura de los mensajes enviados por la Cola (Minero -> Comprobador).
 */
typedef struct {
    uint32_t target;     /**< Objetivo que se intentaba resolver */
    uint32_t solution;   /**< Solución encontrada por el minero */
    pid_t miner_pid;     /**< PID del minero que envía la solución */
    int is_last_miner;   /**< Flag de finalización: 1 si el sistema debe cerrarse[cite: 16] */
} Message;

/**
 * @struct InfoBlock
 * @brief Estructura del bloque que viaja por el Buffer Circular (Comprobador -> Monitor).
 */
typedef struct {
    uint32_t target;     /**< Objetivo de la ronda resuelta */
    uint32_t solution;   /**< Solución que fue procesada */
    pid_t miner_pid;     /**< PID del minero ganador */
    int votes_yes;       /**< Total de votos positivos obtenidos */
    int votes_no;        /**< Total de votos negativos obtenidos */
    int validated;       /**< Resultado de validación: 1 (OK), 0 (Rechazada)[cite: 16] */
    int is_final;        /**< Indica al Monitor que este es el último bloque[cite: 16] */
} InfoBlock;

/**
 * @struct SharedData
 * @brief Estructura principal alojada en la Memoria Compartida.
 * 
 * Contiene la información global de la red (objetivos, carteras, censo), 
 * los semáforos de sincronización y el buffer circular productor-consumidor.[cite: 16]
 */
typedef struct {
    /* --- 1. Información Global de la Red --- */
    uint32_t target;               /**< Objetivo actual para todos los mineros */
    uint32_t solution;             /**< Solución propuesta en la ronda actual */
    pid_t solution_winner;         /**< PID del minero que ha ganado la ronda */

    /* Votaciones */
    int votes_yes;                 /**< Contador de votos afirmativos */
    int votes_no;                  /**< Contador de votos negativos */
    int hay_ganador;               /**< Flag: 1 si un minero ya ha reclamado la victoria */
    sem_t sem_mutex_votes;         /**< Mutex para proteger el recuento de votos */
    
    /* Mineros activos */
    int num_active_miners;         /**< Número total de mineros inscritos en la red */
    pid_t active_miners[MAX_MINERS_GLOBAL]; /**< Censo de PIDs de mineros activos */
    int wallets[MAX_MINERS_GLOBAL];         /**< Carteras (monedas) de todos los procesos[cite: 16] */
    char votes[MAX_MINERS_GLOBAL];          /**< Registro temporal de votos de la ronda */
    
    int validation_result;         /**< Resultado final de la validación del Comprobador */

    /* Semáforos para proteger la información global de la red */
    sem_t sem_mutex_red;           /**< Mutex principal para el acceso a SharedData */
    sem_t sem_votes;               /**< Semáforo para que el ganador espere a los votantes */

    /* --- 2. Buffer Circular (Productor / Consumidor) --- */
    InfoBlock buffer[BUFFER_CIRCULAR_SIZE]; /**< Almacén de bloques para el Monitor[cite: 16] */
    int head;                      /**< Índice de escritura (Comprobador) */
    int tail;                      /**< Índice de lectura (Monitor) */
    
    /* Semáforos para la gestión del Buffer Circular */
    sem_t sem_empty;               /**< Espacios libres en el buffer[cite: 16] */
    sem_t sem_fill;                /**< Espacios ocupados con bloques listos[cite: 16] */
    sem_t sem_mutex_buffer;        /**< Mutex para la exclusión mutua en el buffer[cite: 16] */

    /* Semáforos para pasar de ronda */
    sem_t sem_data_act;            /**< Indica que los datos están listos para el Logger */
    sem_t sem_ready_next_round;    /**< Barrera para iniciar la siguiente ronda */
    sem_t sem_loggers_printed;     /**< Indica que todos los loggers han terminado su escritura */
    int loggers_finished;          /**< Contador para la barrera de loggers */

    /* Semáforo para aceptar nuevos mineros */
    sem_t sem_inscripcion;         /**< Gestiona la entrada controlada de mineros a la red */
} SharedData;

#endif /* MONITOR_H */