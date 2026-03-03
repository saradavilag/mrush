#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static int read_all(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    size_t left = n;

    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;              // error real
        }
        if (r == 0) {
            return 0;               // EOF
        }
        p += (size_t)r;
        left -= (size_t)r;
    }
    return 1;                       // leído completo
}

int logger_run(int read_fd, int write_fd) {
    (void)write_fd; // no usamos ACK (evita SIGPIPE)

    char filename[128];
    int log_fd;
    pid_t parent_pid = getppid();

    mkdir("log", 0755);

    snprintf(filename, sizeof(filename), "log/%d.log", parent_pid);

    log_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        perror("open log file");
        return EXIT_FAILURE;
    }

    while (1) {
        log_args msg;

        int ok = read_all(read_fd, &msg, sizeof(msg));
        if (ok < 0) {
            perror("read pipe");
            close(log_fd);
            return EXIT_FAILURE;
        }
        if (ok == 0) {
            break; // EOF
        }

        if (msg.round == -1)
            break;

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
    }

    close(log_fd);
    close(read_fd);
    return EXIT_SUCCESS;
}