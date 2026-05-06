/**
 * @file monitor.h
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Estructuras de Memoria Compartida y Colas de Mensajes (Práctica 3).
 *
 * Centraliza las definiciones necesarias para la comunicación entre la red de
 * Mineros y el sistema central (Comprobador/Monitor) sustituyendo los antiguos
 * ficheros de texto.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <sys/types.h>
#include <semaphore.h>

/* Nombres unificados para los mecanismos IPC POSIX */
#define SHM_NAME "/shm_miner_rush"
#define MQ_NAME  "/mq_miner_rush"

/* Constantes de dimensionamiento exigidas por el enunciado */
#define MAX_MINERS_GLOBAL 100 
#define BUFFER_CIRCULAR_SIZE 6 
#define MQ_MAX_MSG 7          

/**
 * @struct Message
 * @brief Estructura de los mensajes enviados por la Cola (Minero -> Comprobador).
 */
typedef struct {
    uint32_t target;
    uint32_t solution;
    pid_t miner_pid;
    int is_last_miner;  /* 1 si es el mensaje de finalización del sistema */
} Message;

/**
 * @struct InfoBlock
 * @brief Estructura del bloque que viaja por el Buffer Circular (Comprobador -> Monitor).
 */
typedef struct {
    uint32_t target;
    uint32_t solution;
    pid_t miner_pid;
    int votes_yes;
    int votes_no;
    int validated;      /* 1 si la solución es correcta, 0 si fue rechazada */
    int is_final;       /* 1 para indicar al Monitor que debe terminar */
} InfoBlock;

/**
 * @struct SharedData
 * @brief Estructura principal alojada en la Memoria Compartida.
 */
typedef struct {
    /* --- 1. Información Global de la Red --- */
    uint32_t target;
    uint32_t solution;
    pid_t solution_winner;

    /* Votaciones */
    int votes_yes;
    int votes_no;
    int hay_ganador;
    sem_t sem_mutex_votes;
    
    /* Minero activos */
    int num_active_miners;
    pid_t active_miners[MAX_MINERS_GLOBAL];
    int wallets[MAX_MINERS_GLOBAL];         
    char votes[MAX_MINERS_GLOBAL];           
    
    int validation_result;              

    /* Semáforos para proteger la información global de la red */
    sem_t sem_mutex_red; 
    sem_t sem_votes;

    /* --- 2. Buffer Circular (Productor / Consumidor) --- */
    InfoBlock buffer[BUFFER_CIRCULAR_SIZE];
    int head; /* Índice de escritura (Productor) */
    int tail; /* Índice de lectura (Consumidor) */
    
    /* Semáforos para la gestión del Buffer Circular */
    sem_t sem_empty; 
    sem_t sem_fill;  
    sem_t sem_mutex_buffer; 

    /* Semaforo para pasar de ronda */
    sem_t sem_data_act;
    sem_t sem_ready_next_round; 
    sem_t sem_loggers_printed; 
    int loggers_finished;

    /* Semáforo para aceptar nuevos mineros */
    sem_t sem_inscripcion;
} SharedData;

#endif /* MONITOR_H */