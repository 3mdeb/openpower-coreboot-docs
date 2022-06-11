#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 128

#define SOCKET_NAME "\0obmc-console"
#define SOCKET_NAME_LEN (sizeof(SOCKET_NAME) - 1)

void signal_noop(int s)
{
}

int main(int argc, char *argv[])
{
    struct sockaddr_un addr;
    int ret;
    int sock;
    char buffer[BUFFER_SIZE];
    struct sigaction action;

    // Make Ctrl+C cause read() to return EINTR
    action.sa_handler = &signal_noop;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, SOCKET_NAME, SOCKET_NAME_LEN);

    ret = connect(sock,
                  (const struct sockaddr *)&addr,
                  sizeof(addr) - sizeof(addr.sun_path) + SOCKET_NAME_LEN);
    if (ret == -1) {
        fprintf(stderr, "Can't connect: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (1) {
        ret = read(sock, buffer, sizeof(buffer) - 1);
        if (ret == 0)
            break;
        if (ret == -1) {
            if (errno == EINTR) {
                // Do an async read() to get last portion of data or we won't
                // miss anything anyway?
                break;
            }

            perror("read");
            exit(EXIT_FAILURE);
        }

        buffer[ret - 1] = '\0';
        fputs(buffer, stdout);
    }

    close(sock);
    return EXIT_SUCCESS;
}
