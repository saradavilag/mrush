/**
 * @file main.c
 * @author Sara, Marco
 * @date 2026-05-01
 * @brief Punto de entrada del programa ./miner para la Práctica 3.
 *
 * Responsabilidades:
 * - Parsear argumentos: <N_SECS> <N_THREADS>.
 * - Abrir la Memoria Compartida y la Cola de Mensajes creadas por el Monitor.
 * - Gestionar señales (SIGALRM, SIGINT, SIGUSR2).
 * - Crear el proceso Logger local vía tubería (pipe).
 * - Registrar al minero en la red, lanzar miner_run() y desregistrarlo al salir.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <mqueue.h>

#include "types.h"
#include "miner.h"
#include "logger.h"
#include "monitor.h" 

/* Variables globales exportadas para el miner.c */
volatile sig_atomic_t got_sig_exit = 0;
volatile sig_atomic_t got_sig_usr2 = 0;
sigset_t block_mask;
sigset_t wait_mask_usr1;
sigset_t wait_mask_usr2;

/**
 * @brief Manejador para señales de finalización.
 * 
 * Se ejecuta al recibir SIGALRM (fin de tiempo del minero) o SIGINT (Ctrl+C).
 * Activa la bandera global got_sig_exit para que el minero abandone el bucle
 * de minado limpiamente en la siguiente comprobación.
 * 
 * @param sig Número de la señal recibida.
 */
static void handle_exit_sig(int sig) {
    (void)sig;
    got_sig_exit = 1;
}

/**
 * @brief Manejador para despertar del sigsuspend (Votación).
 * 
 * Se ejecuta al recibir SIGUSR2. El minero ganador envía esta señal a la red 
 * para avisar de que ha encontrado una solución y se debe proceder a votar.
 * Cambia la bandera got_sig_usr2 para interrumpir los hilos de búsqueda.
 * 
 * @param sig Número de la señal recibida.
 */
static void handle_usr2(int sig) {
    (void)sig; 
    got_sig_usr2 = 1;
}

/**
 * @brief Manejador vacío para despertar del sigsuspend (Nueva Ronda).
 * 
 * Se ejecuta al recibir SIGUSR1. El minero ganador de la ronda anterior 
 * envía esta señal para dar el pistoletazo de salida a la nueva ronda.
 * No necesita modificar variables, solo interrumpir el sigsuspend.
 * 
 * @param sig Número de la señal recibida.
 */
static void handle_usr1(int sig) {
    (void)sig;
}

/**
 * @brief Función principal del proceso Minero.
 * 
 * Configura el entorno del minero (IPC, señales, procesos hijos) y delega
 * la lógica de minado a la función miner_run(). Al finalizar, limpia los 
 * recursos locales y avisa al sistema de su salida.
 * 
 * @param argc Número de argumentos pasados por línea de comandos.
 * @param argv Array de argumentos. Se esperan <N_SECS> y <N_THREADS>.
 * @return EXIT_SUCCESS si el programa finaliza correctamente, EXIT_FAILURE en caso de error.
 */
int main(int argc, char *argv[]) {
    long n_secs, n_threads;
    int shm_fd;
    SharedData *shm;
    mqd_t mq;
    pid_t logger_pid;
    int pipe_fd[2];
    int mi_indice;

    /* 1. Parseo de argumentos */
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <N_SECS> <N_THREADS>\n", argv[0]);
        return EXIT_FAILURE;
    }
    n_secs = strtol(argv[1], NULL, 10);
    n_threads = strtol(argv[2], NULL, 10);

    /* BLOQUEAMOS las señales fuera del sigsuspend para no perderlas en posibles condiciones de carrera */
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);
    sigaddset(&block_mask, SIGUSR2);
    sigaddset(&block_mask, SIGALRM);
    sigprocmask(SIG_BLOCK, &block_mask, NULL);
    
    /* 2. Configuración estricta de señales */
    struct sigaction act = {0};
    act.sa_handler = handle_exit_sig;
    sigaction(SIGALRM, &act, NULL);
    sigaction(SIGINT, &act, NULL); 

    struct sigaction act_usr1 = {0};
    act_usr1.sa_handler = handle_usr1;
    sigaction(SIGUSR1, &act_usr1, NULL);

    struct sigaction act_usr2 = {0};
    act_usr2.sa_handler = handle_usr2;
    sigaction(SIGUSR2, &act_usr2, NULL);

    /* Preparamos las máscaras para sigsuspend */
    sigfillset(&wait_mask_usr1);
    sigdelset(&wait_mask_usr1, SIGUSR1);
    sigdelset(&wait_mask_usr1, SIGALRM);

    sigfillset(&wait_mask_usr2);
    sigdelset(&wait_mask_usr2, SIGUSR2);

    /* Abrir Memoria Compartida (El Comprobador debe haberla creado) */
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open (¿Está el monitor ejecutándose?)");
        return EXIT_FAILURE;
    }
    shm = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); // Ya mapeado, podemos cerrar el FD

    /* Abrir Cola de Mensajes */
    mq = mq_open(MQ_NAME, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        munmap(shm, sizeof(SharedData));
        return EXIT_FAILURE;
    }

    /* Tubería y Logger */
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }
    logger_pid = fork();
    if (logger_pid == 0) {
        close(pipe_fd[1]);
        exit(logger_run(pipe_fd[0], -1));
    }
    close(pipe_fd[0]);
    int log_fd = pipe_fd[1];

    /* 6. Lógica de Minado (P3) */
    mi_indice = miner_add_system(shm);
    if (mi_indice == -1) {
        fprintf(stderr, "Red llena o error de registro.\n");
    } else {
        alarm((unsigned int)n_secs);
        
        /* Ejecuta el bucle bloqueante que hicimos en miner.c */
        miner_run(log_fd, shm, mq, mi_indice, (int)n_threads);
        
        /* Se despide de la red e informa si es el último */
        miner_del_system(shm, mq, mi_indice);
    }

    /* 7. Limpieza final */
    log_args exit_msg = {0};
    exit_msg.round = -1;
    if (write(log_fd, &exit_msg, sizeof(exit_msg)) == -1) {
        perror("write exit_msg"); // Silencia el warning
    }
    close(log_fd);
    waitpid(logger_pid, NULL, 0);

    mq_close(mq);
    munmap(shm, sizeof(SharedData));

    return EXIT_SUCCESS;
}