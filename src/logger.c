#include "logger.h"

/* Estructura para enviar los datos de cada ronda */
typedef struct {
    int round;          // Número de ronda, -1 indica final
    uint32_t target;    // Objetivo de la ronda
    uint32_t solution;  // Solución encontrada
    int valid;          // 1 = validated, 0 = rejected
} log_msg_t;

/* Función del registrador */
int logger_run(int read_fd, int write_fd) {
    char filename[64];
    int log_fd;
    pid_t parent_pid = getppid();

    snprintf(filename, sizeof(filename), "%d.log", parent_pid);

    log_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        perror("open log file");
        return EXIT_FAILURE;
    }

    while (1) {
        log_msg_t msg;
        ssize_t n = read(read_fd, &msg, sizeof(msg));
        if (n < 0) {
            perror("read pipe");
            close(log_fd);
            return EXIT_FAILURE;
        } else if (n == 0) {
            // EOF: tubería cerrada por Minero
            break;
        }

        // Señal de finalización
        if (msg.round == -1)
            break;

        // Escribir datos en el fichero
        int ret = dprintf(log_fd,
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
            msg.round
        );

        if (ret < 0) {
            perror("dprintf");
            close(log_fd);
            return EXIT_FAILURE;
        }

        // Enviar confirmación al Minero
        int ack = 1;
        if (write(write_fd, &ack, sizeof(ack)) < 0) {
            perror("write ack");
            close(log_fd);
            return EXIT_FAILURE;
        }
    }

    close(log_fd);
    return EXIT_SUCCESS;
}