#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#define SERVER_FIFO "serverFIFO"

struct message {
    char source[50];
    char target[50];
    char msg[200];
};

void terminate(int sig) {
    (void)sig;
    printf("Exiting....\n");
    fflush(stdout);
    exit(0);
}

int main(void) {
    int server;
    int dummyfd;
    struct message req;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, terminate);

    server = open(SERVER_FIFO, O_RDONLY);
    if (server < 0) {
        perror("open serverFIFO");
        return 1;
    }

    dummyfd = open(SERVER_FIFO, O_WRONLY);
    if (dummyfd < 0) {
        perror("open dummy serverFIFO");
    }

    while (1) {
        ssize_t n = read(server, &req, sizeof(req));
        if (n <= 0) {
            continue;
        }

        printf("Received a request from %s to send the message %s to %s.\n",
               req.source, req.msg, req.target);
        fflush(stdout);

        int target = open(req.target, O_WRONLY);
        if (target < 0) {
            perror("open target fifo");
            continue;
        }

        if (write(target, &req, sizeof(req)) != sizeof(req)) {
            perror("write target fifo");
        }

        close(target);
    }

    close(server);
    if (dummyfd >= 0)
        close(dummyfd);
    return 0;
}
