#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>

#define N 13
#define SERVER_FIFO "serverFIFO"

extern char **environ;
char uName[20];

char *allowed[N] = {
    "cp","touch","mkdir","ls","pwd","cat","grep",
    "chmod","diff","cd","exit","help","sendmsg"
};

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

int isAllowed(const char *cmd) {
    for (int i = 0; i < N; i++) {
        if (strcmp(cmd, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void sendmsg_to_user(const char *target, const char *msg) {
    int fd = open(SERVER_FIFO, O_WRONLY);
    if (fd < 0) {
        perror("open serverFIFO");
        return;
    }

    struct message m;
    strncpy(m.source, uName, sizeof(m.source) - 1);
    m.source[sizeof(m.source) - 1] = '\0';

    strncpy(m.target, target, sizeof(m.target) - 1);
    m.target[sizeof(m.target) - 1] = '\0';

    strncpy(m.msg, msg, sizeof(m.msg) - 1);
    m.msg[sizeof(m.msg) - 1] = '\0';

    if (write(fd, &m, sizeof(m)) != sizeof(m)) {
        perror("write serverFIFO");
    }

    close(fd);
}

void *messageListener(void *arg) {
    (void)arg;

    /* each user has a FIFO with their username as the name */
    int fd = open(uName, O_RDONLY);
    if (fd < 0) {
        perror("open user fifo");
        return NULL;
    }

    struct message m;
    while (1) {
        ssize_t n = read(fd, &m, sizeof(m));
        if (n <= 0) {
            continue;
        }
        printf("Incoming message from %s: %s\n", m.source, m.msg);
        fflush(stdout);
    }

    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    pid_t pid;
    char **cargv;
    char *path;
    char line[256];
    int status;
    pthread_t listenerThread;

    if (argc != 2) {
        printf("Usage: ./rsh <username>\n");
        exit(1);
    }

    signal(SIGINT, terminate);

    strncpy(uName, argv[1], sizeof(uName) - 1);
    uName[sizeof(uName) - 1] = '\0';

    /* create the message listener thread */
    if (pthread_create(&listenerThread, NULL, messageListener, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }

    while (1) {
        fprintf(stderr, "rsh>");

        if (fgets(line, sizeof(line), stdin) == NULL)
            continue;

        if (strcmp(line, "\n") == 0)
            continue;

        line[strcspn(line, "\n")] = '\0';   /* strip newline */

        char cmd[256];
        char line2[256];

        strcpy(line2, line);

        char *token = strtok(line, " ");
        if (token == NULL)
            continue;

        strcpy(cmd, token);

        if (!isAllowed(cmd)) {
            printf("NOT ALLOWED!\n");
            continue;
        }

        if (strcmp(cmd, "sendmsg") == 0) {
            /* line2: "sendmsg target message..." */
            char *p = strchr(line2, ' ');
            if (!p) {
                printf("sendmsg: you have to specify target user and message\n");
                continue;
            }

            while (*p == ' ')
                p++;

            if (*p == '\0') {
                printf("sendmsg: you have to specify target user and message\n");
                continue;
            }

            char *target = p;
            char *space = strchr(target, ' ');
            if (!space) {
                printf("sendmsg: you have to enter a message\n");
                continue;
            }

            *space = '\0';
            char *msg = space + 1;
            while (*msg == ' ')
                msg++;

            if (*msg == '\0') {
                printf("sendmsg: you have to enter a message\n");
                continue;
            }

            sendmsg_to_user(target, msg);
            continue;
        }

        if (strcmp(cmd, "exit") == 0)
            break;

        if (strcmp(cmd, "cd") == 0) {
            char *targetDir = strtok(NULL, " ");
            if (strtok(NULL, " ") != NULL) {
                printf("-rsh: cd: too many arguments\n");
            } else if (targetDir != NULL) {
                if (chdir(targetDir) != 0) {
                    perror("cd");
                }
            }
            continue;
        }

        if (strcmp(cmd, "help") == 0) {
            printf("The allowed commands are:\n");
            for (int i = 0; i < N; i++) {
                printf("%d: %s\n", i + 1, allowed[i]);
            }
            continue;
        }

        /* build argv for external command */
        cargv = (char **)malloc(sizeof(char *));
        if (!cargv) {
            perror("malloc");
            exit(1);
        }

        cargv[0] = strdup(cmd);
        if (!cargv[0]) {
            perror("strdup");
            exit(1);
        }

        path = strdup(cmd);
        if (!path) {
            perror("strdup");
            exit(1);
        }

        char *attrToken = strtok(line2, " "); /* skip command itself */
        attrToken = strtok(NULL, " ");
        int n = 1;
        while (attrToken != NULL) {
            n++;
            cargv = (char **)realloc(cargv, sizeof(char *) * n);
            if (!cargv) {
                perror("realloc");
                exit(1);
            }
            cargv[n - 1] = strdup(attrToken);
            if (!cargv[n - 1]) {
                perror("strdup");
                exit(1);
            }
            attrToken = strtok(NULL, " ");
        }
        cargv = (char **)realloc(cargv, sizeof(char *) * (n + 1));
        if (!cargv) {
            perror("realloc");
            exit(1);
        }
        cargv[n] = NULL;

        if (posix_spawnp(&pid, path, NULL, NULL, cargv, environ) != 0) {
            perror("spawn failed");
        } else {
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid failed");
            }
        }

        for (int i = 0; i < n; i++) {
            free(cargv[i]);
        }
        free(cargv);
        free(path);
    }

    return 0;
}
