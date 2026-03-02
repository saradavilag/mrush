#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
 read_fd  -> recibe mensajes del MINER
 write_fd -> envía ACK al MINER
*/

int logger_run(int read_fd, int write_fd)
{
    char filename[128];
    int log_fd;
    pid_t parent_pid = getppid();

    /* Crear carpeta log si no existe */
    mkdir("log", 0755);   // no falla si ya existe

    /* Nombre del fichero: log/<ppid>.log */
    snprintf(filename, sizeof(filename),
             "log/%d.log", parent_pid);

    log_fd = open(filename,
                  O_WRONLY | O_CREAT | O_APPEND,
                  0644);

    if (log_fd < 0) {
        perror("open log file");
        return EXIT_FAILURE;
    }

    while (1) {

        log_args msg;

        ssize_t n = read(read_fd, &msg, sizeof(msg));

        if (n < 0) {
            perror("read pipe");
            close(log_fd);
            return EXIT_FAILURE;
        }

        /* EOF: el miner cerró la tubería */
        if (n == 0)
            break;

        /* mensaje de finalización */
        if (msg.round == -1)
            break;

        /* escribir bloque en fichero */
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

        /* --- enviar ACK al miner --- */
        int ack = 1;

        if (write(write_fd, &ack, sizeof(ack)) != sizeof(ack)) {
            perror("write ack");
            close(log_fd);
            return EXIT_FAILURE;
        }
    }

    close(log_fd);
    close(read_fd);
    close(write_fd);

    return EXIT_SUCCESS;
}