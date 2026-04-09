/**
 * @file main.c
 * @author Sara, Marco
 * @date 2026-04-02
 * @brief Punto de entrada del programa ./miner (Miner Rush - minero único).
 *
 * Responsabilidades:
 * - Parsear argumentos de línea de comandos: TARGET_INI, ROUNDS, N_THREADS.
 * - Crear las tuberías necesarias para la comunicación Miner -> Logger (y canal inverso).
 * - Crear el proceso Logger con fork().
 * - Ejecutar el minado (miner_run) en el proceso padre.
 * - Esperar al proceso Logger y mostrar su código de salida según el enunciado.
 *
 * Dependencias:
 * - miner.h: interfaz del minero.
 * - logger.h: interfaz del logger (incluida por miner.h).
 * - sys/wait.h: waitpid y macros WIFEXITED/WEXITSTATUS.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/wait.h> 

#include "managers.h"
#include "pow.h"
#include "logger.h"   

#define VOTE_WAIT_TRIES 30
#define VOTE_WAIT_USEC 100000

static volatile sig_atomic_t stop_flag = 0;
static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigusr2 = 0;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t target;
    uint32_t *solution;
    int *found;
    pthread_mutex_t *mutex;
} worker_args_t;

/**
 * @brief Manejador para la señal de temporización (SIGALRM).
 *
 * Activa el flag global de parada cuando el proceso minero ha agotado
 * su tiempo de vida estipulado por los argumentos de entrada.
 *
 * @param sig Número de la señal recibida (ignorado en el cuerpo).
 */
static void handle_alarm(int sig) {
    (void)sig;
    stop_flag = 1;
}

/**
 * @brief Manejador para la señal de inicio de ronda (SIGUSR1).
 *
 * Activa un flag indicando que un minero ha dado el pistoletazo de salida
 * para que todos comiencen a calcular el Proof of Work.
 *
 * @param sig Número de la señal recibida (ignorado en el cuerpo).
 */
static void handle_sigusr1(int sig) {
    (void)sig;
    got_sigusr1 = 1;
}

/**
 * @brief Manejador para la señal de alto el fuego (SIGUSR2).
 *
 * Activa un flag indicando que algún minero de la red ha encontrado
 * una solución válida, por lo que se debe abortar el minado actual y pasar a votar.
 *
 * @param sig Número de la señal recibida (ignorado en el cuerpo).
 */
static void handle_sigusr2(int sig) {
    (void)sig;
    got_sigusr2 = 1;
}

/**
 * @brief Instala un manejador para una señal específica.
 *
 * Envuelve la llamada a sigaction() garantizando que la máscara inicial 
 * y las banderas (flags) de la señal queden a cero de forma segura.
 *
 * @param signum Número identificador de la señal a capturar.
 * @param handler Puntero a la función manejadora de la señal.
 * @return 0 en caso de éxito, -1 si ocurre un error (errno preservado).
 */
static int install_handler(int signum, void (*handler)(int)) {
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    sigemptyset(&(act.sa_mask));
    act.sa_flags = 0;

    if (sigaction(signum, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

/**
 * @brief Escribe exactamente n bytes en un descriptor de fichero.
 *
 * Previene el problema de las escrituras parciales en tuberías (pipes) 
 * iterando llamadas a write() hasta haber volcado el buffer completo.
 *
 * @param fd Descriptor de fichero donde escribir (extremo de escritura del pipe).
 * @param buf Puntero al inicio de los datos a escribir.
 * @param n Número exacto de bytes que se deben transferir.
 * @return 0 si se han escrito todos los bytes con éxito, -1 en caso de error grave.
 */
static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

/**
 * @brief Tarea ejecutada concurrentemente por cada hilo minero.
 *
 * Recorre un fragmento del espacio de búsqueda comprobando el hash. Si encuentra
 * la coincidencia, adquiere el mutex para apuntar la solución de forma segura 
 * y avisa a los demás alterando el flag atómico compartido.
 *
 * @param arg Puntero genérico a la estructura worker_args_t con los límites y variables compartidas.
 * @return NULL al finalizar el trabajo.
 */
static void *worker(void *arg) {
    worker_args_t *a = (worker_args_t *)arg;

    for (uint32_t i = a->start; i < a->end; ++i) {
        if (stop_flag || got_sigusr2) {
            break;
        }

        if (__atomic_load_n(a->found, __ATOMIC_ACQUIRE)) {
            break;
        }

        if ((uint32_t)pow_hash((long int)i) == a->target) {
            pthread_mutex_lock(a->mutex);
            if (!*(a->found)) {
                *(a->found) = 1;
                *(a->solution) = i;
            }
            pthread_mutex_unlock(a->mutex);
            break;
        }
    }

    return NULL;
}

/**
 * @brief Gestiona el proceso de minado distribuyendo el trabajo en hilos.
 *
 * Divide equitativamente el espacio de búsqueda (POW_LIMIT) entre el número de hilos 
 * especificado, los lanza y espera a que terminen mediante pthread_join.
 *
 * @param target Número objetivo (hash) que se debe conseguir en esta ronda.
 * @param n_threads Cantidad de hilos (trabajadores) a generar.
 * @param solution Puntero donde se almacenará el número origen que generó el hash correcto.
 * @return 1 si se encontró la solución, 0 si el rango se agotó sin éxito, -1 si falló la creación de hilos.
 */
static int mine_solution(uint32_t target, int n_threads, uint32_t *solution) {
    pthread_t *tids = NULL;
    worker_args_t *args = NULL;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int found = 0;
    uint32_t base, rem, start = 0;

    tids = calloc((size_t)n_threads, sizeof(*tids));
    args = calloc((size_t)n_threads, sizeof(*args));
    if (tids == NULL || args == NULL) {
        perror("calloc");
        free(tids);
        free(args);
        return -1;
    }

    base = (uint32_t)POW_LIMIT / (uint32_t)n_threads;
    rem  = (uint32_t)POW_LIMIT % (uint32_t)n_threads;

    for (int k = 0; k < n_threads; ++k) {
        uint32_t chunk = base + ((uint32_t)k < rem ? 1U : 0U);
        uint32_t end = start + chunk;

        args[k].start = start;
        args[k].end = end;
        args[k].target = target;
        args[k].solution = solution;
        args[k].found = &found;
        args[k].mutex = &mutex;

        if (pthread_create(&tids[k], NULL, worker, &args[k]) != 0) {
            perror("pthread_create");
            for (int j = 0; j < k; ++j) pthread_join(tids[j], NULL);
            free(tids);
            free(args);
            return -1;
        }

        start = end;
    }

    for (int k = 0; k < n_threads; ++k) {
        pthread_join(tids[k], NULL);
    }

    free(tids);
    free(args);

    return found ? 1 : 0;
}

/**
 * @brief Difunde una señal a todos los mineros de la red.
 *
 * Emite la señal indicada a todos los procesos registrados en el vector 
 * de asistentes, excluyendo deliberadamente al propio emisor.
 *
 * @param pids Array que contiene los IDs de los procesos mineros conectados.
 * @param count Cantidad actual de elementos útiles en el array.
 * @param self ID del proceso actual (para evitar enviarse la señal a sí mismo).
 * @param signum Señal que se desea propagar (e.g., SIGUSR1 o SIGUSR2).
 * @return 0 en caso de éxito global, -1 si falla el envío a un proceso vivo.
 */
static int send_signal_to_list(const pid_t pids[], size_t count, pid_t self, int signum) {
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == self) continue;
        if (kill(pids[i], signum) == -1) {
            if (errno != ESRCH) {
                perror("kill");
                return -1;
            }
        }
    }
    return 0;
}

/**
 * @brief Wrapper seguro para contar el censo de mineros actuales.
 *
 * Utiliza la infraestructura del manager (y sus semáforos) para recuperar la lista 
 * actualizada de participantes desde el archivo MINERS_FILE.
 *
 * @param miners_sem Semáforo POSIX que protege la lista global de mineros.
 * @param count Puntero de salida donde se guardará el número de mineros contados.
 * @param pids Array de salida donde se cargarán los PIDs leídos.
 * @return 0 en caso de éxito, -1 si ocurre un error de lectura o bloqueo.
 */
static int count_miners(sem_t *miners_sem, size_t *count, pid_t pids[]) {
    return managers_read_pids(miners_sem, pids, MAX_MINERS, count);
}

/**
 * @brief Valida criptográficamente una solución ajena (Proceso de consenso).
 *
 * Ejecuta la prueba de trabajo sobre la propuesta de otro minero para dictaminar
 * si el voto individual será positivo ('Y') o negativo ('N').
 *
 * @param target Objetivo que debía ser resuelto en la ronda actual.
 * @param solution Solución reclamada por el minero ganador temporal.
 * @return 'Y' (carácter ASCII) si la prueba matemática es correcta, 'N' en caso contrario.
 */
static int vote_validity(uint32_t target, uint32_t solution) {
    return ((uint32_t)pow_hash((long int)solution) == target) ? 'Y' : 'N';
}

/**
 * @brief Muestra por salida estándar el resumen gráfico del recuento de una votación.
 *
 * @param winner PID del minero sometido a escrutinio.
 * @param votes Array con las papeletas literales registradas ('Y' y 'N').
 * @param count Total de papeletas recuperadas de la urna.
 * @param accepted Entero booleano (1 si la propuesta superó la votación, 0 si no).
 */
static void print_votes_line(pid_t winner, const char votes[], size_t count, int accepted) {
    printf("Winner %d => [", (int)winner);
    for (size_t i = 0; i < count; ++i) {
        printf(" %c", votes[i]);
    }
    printf(" ] => %s\n", accepted ? "Accepted" : "Rejected");
    fflush(stdout);
}

sem_t *mutex = NULL;
int n_miners = 0;

/**
 * @brief Función de inicialización base (Auxiliar).
 *
 * Abre o crea un semáforo POSIX genérico de exclusión mutua ("mutex") 
 * verificando los errores de sem_open.
 *
 * @return EXIT_SUCCESS si el semáforo se levanta correctamente, EXIT_FAILURE si falla.
 */
int init_system(){
    if ((mutex = sem_open("mutex", O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED){
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Bucle principal de la aplicación minero.
 *
 * Orquesta la lógica del programa: parseo de argumentos, creación del registrador (Logger),
 * gestión de la señal SIGALRM, y el bucle infinito de rondas blockchain (minado, proclamación
 * de victoria y validación por consenso).
 *
 * @param argc Cantidad de argumentos pasados por consola.
 * @param argv Valores de los argumentos (1: segundos de vida, 2: número de hilos).
 * @return EXIT_SUCCESS al completar el ciclo de vida sin errores críticos.
 */
int main(int argc, char *argv[]) {
    long n_secs;
    long n_threads;
    pid_t self = getpid();
    sem_t *miners_sem = NULL;
    sem_t *target_sem = NULL;
    sem_t *votes_sem = NULL;
    sem_t *winner_sem = NULL;
    int is_first_miner = 0;
    int started_any_round = 0;
    int coins = 0;
    pid_t participants[MAX_MINERS];
    size_t participants_count = 0;
    round_info_t info;

    sigset_t block_mask, susp_mask;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <N_SECS> <N_THREADS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    n_secs = strtol(argv[1], NULL, 10);
    n_threads = strtol(argv[2], NULL, 10);

    if (n_secs <= 0 || n_threads <= 0) {
        fprintf(stderr, "Both <N_SECS> and <N_THREADS> must be > 0\n");
        return EXIT_FAILURE;
    }

    if (install_handler(SIGALRM, handle_alarm) == -1) return EXIT_FAILURE;
    if (install_handler(SIGUSR1, handle_sigusr1) == -1) return EXIT_FAILURE;
    if (install_handler(SIGUSR2, handle_sigusr2) == -1) return EXIT_FAILURE;

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);
    sigaddset(&block_mask, SIGUSR2);

    if (sigprocmask(SIG_BLOCK, &block_mask, &susp_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    int pipe_fd[2];
    pid_t logger_pid;

    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    logger_pid = fork();
    if (logger_pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (logger_pid == 0) {
        close(pipe_fd[1]); 
        exit(logger_run(pipe_fd[0], -1));
    }

    close(pipe_fd[0]); 
    int log_fd = pipe_fd[1];

    if (managers_open_all(&miners_sem, &target_sem, &votes_sem, &winner_sem) == -1) {
        return EXIT_FAILURE;
    }

    if (managers_add_miner(miners_sem, self, &is_first_miner) == -1) {
        managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
        return EXIT_FAILURE;
    }

    if (is_first_miner) {
        if (target_init_if_needed(target_sem) == -1) {
            managers_remove_miner(miners_sem, self);
            managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
            return EXIT_FAILURE;
        }
    }

    alarm((unsigned int)n_secs);

    while (!stop_flag) {
        size_t count = 0;

        if (count_miners(miners_sem, &count, participants) == -1) {
            break;
        }

        if (count < 2) {
            usleep(200000);
            continue;
        }

        if (is_first_miner && !started_any_round) {
            if (target_read_info(target_sem, &info) == -1) break;
            info.winner = -1;
            info.solution = 0;
            if (target_write_info(target_sem, &info) == -1) break;

            if (send_signal_to_list(participants, count, self, SIGUSR1) == -1) break;
            got_sigusr1 = 1;
            started_any_round = 1;
        }

        if (!got_sigusr1) {
            sigsuspend(&susp_mask);
            continue;
        }

        got_sigusr1 = 0;
        got_sigusr2 = 0;

        if (count_miners(miners_sem, &participants_count, participants) == -1) {
            break;
        }

        if (participants_count < 2) {
            continue;
        }

        if (target_read_info(target_sem, &info) == -1) {
            break;
        }

        printf("Miner %d starts round %u with target %u and %ld threads\n",
               (int)self, info.round, info.target, n_threads);
        fflush(stdout);

        uint32_t solution = 0;
        int mine_res = mine_solution(info.target, (int)n_threads, &solution);
        if (mine_res == -1) break;

        int i_am_winner = 0;

        if (!got_sigusr2 && mine_res == 1) {
            if (sem_wait(winner_sem) == -1) {
                perror("sem_wait winner_sem");
                break;
            }

            round_info_t current;
            if (target_read_info(target_sem, &current) == -1) {
                sem_post(winner_sem);
                break;
            }

            if (current.round == info.round && current.winner == -1) {
                current.winner = self;
                current.solution = solution;

                if (target_write_info(target_sem, &current) == -1) {
                    sem_post(winner_sem);
                    break;
                }

                if (votes_reset(votes_sem) == -1) {
                    sem_post(winner_sem);
                    break;
                }

                i_am_winner = 1;
            }

            if (sem_post(winner_sem) == -1) {
                perror("sem_post winner_sem");
                break;
            }

            if (i_am_winner) {
                if (send_signal_to_list(participants, participants_count, self, SIGUSR2) == -1) {
                    break;
                }
                got_sigusr2 = 1;
            }
        }

        while (!got_sigusr2 && !stop_flag) {
            sigsuspend(&susp_mask);
        }

        if (stop_flag) break;

        if (target_read_info(target_sem, &info) == -1) {
            break;
        }

        if (info.winner != -1) {
            char my_vote = (char)vote_validity(info.target, info.solution);
            if (votes_add(votes_sem, my_vote) == -1) {
                break;
            }
        }

        if (i_am_winner) {
            char votes[MAX_VOTES];
            size_t votes_count = 0;
            int yes = 0, no = 0;
            int tries = 0;
            int accepted = 0;

            while (tries < VOTE_WAIT_TRIES) {
                if (votes_read(votes_sem, votes, MAX_VOTES, &votes_count, &yes, &no) == -1) {
                    break;
                }

                if (votes_count >= participants_count) {
                    break;
                }

                usleep(VOTE_WAIT_USEC);
                ++tries;
            }

            accepted = (yes >= no);
            if (accepted) {
                ++coins;
            }

            print_votes_line(info.winner, votes, votes_count, accepted);

            log_args msg;
            msg.round = info.round;
            msg.target = info.target;
            msg.solution = info.solution;
            msg.valid = accepted;
            msg.total_votes = votes_count;
            msg.votes_yes = yes;
            msg.coins = coins;
            
            if (write_all(log_fd, &msg, sizeof(msg)) == -1) {
                perror("write_all to logger");
            }

            round_info_t next = info;
            next.round = info.round + 1;
            next.winner = -1;
            next.solution = 0;
            if (accepted) {
                next.target = info.solution;
            }

            if (target_write_info(target_sem, &next) == -1) {
                break;
            }

            if (!stop_flag) {
                if (count_miners(miners_sem, &participants_count, participants) == -1) {
                    break;
                }

                if (participants_count >= 2) {
                    if (send_signal_to_list(participants, participants_count, self, SIGUSR1) == -1) {
                        break;
                    }
                    got_sigusr1 = 1;
                }
            }

            (void)coins;
        }
    }

    if (managers_remove_miner(miners_sem, self) == -1) {
        managers_close_all(miners_sem, target_sem, votes_sem, winner_sem);
        return EXIT_FAILURE;
    }

    if (managers_close_all(miners_sem, target_sem, votes_sem, winner_sem) == -1) {
        return EXIT_FAILURE;
    }

    log_args exit_msg;
    exit_msg.round = -1; 
    write_all(log_fd, &exit_msg, sizeof(exit_msg));
    close(log_fd);
    
    waitpid(logger_pid, NULL, 0);

    return EXIT_SUCCESS;
}