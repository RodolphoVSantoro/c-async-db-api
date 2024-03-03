#include "httpHandler.h"

#define MAX_THREADS 10

int setSocketNonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        printf("Error getting socket flags");
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        printf("Error setting non-blocking mode");
        return -1;
    }

    return 0;
}

int serverSocket;
int dbSockets[MAX_THREADS];

pthread_t acceptConnectionsThread;
pthread_t workerThreads[MAX_THREADS];
pthread_mutex_t queueLock;

int cleanup(int signum) {
    int killAcceptResult = pthread_kill(acceptConnectionsThread, signum);

    int killHandlersResults[MAX_THREADS];
    int closeDbResults[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        killHandlersResults[i] = pthread_kill(acceptConnectionsThread, signum);
        closeDbResults[i] = close(dbSockets[i]);
    }

    int closeServerResult = close(serverSocket);

    for (int i = 0; i < MAX_THREADS; i++) {
        raiseIfError(closeDbResults[i]);
        raiseIfError(killHandlersResults[i]);
    }
    raiseIfError(closeServerResult);
    raiseIfError(killAcceptResult);

    return SUCCESS;
}

// For profiling even if the server closes from a ctrl+c signal
void sigIntHandler(int signum) {
    printf("{ Caught signal %d }\n", signum);
    exit(cleanup(signum));
}

typedef struct HandleConnectionArgs {
    Queue *queue;
    int dbPort;
} HandleConnectionArgs;

void *handleConnections(void *arg) {
    HandleConnectionArgs *args = (HandleConnectionArgs *)arg;
    Queue *queue = args->queue;
    int dbPort = args->dbPort;

    int dbSocket = connectToDb(dbPort);
    if (dbSocket == ERROR) {
        log("{ Error connecting to db on port %d }\n", dbPort);
        exit(1);
    }

    int clientSocket = -1;

    while (true) {
        pthread_mutex_lock(&queueLock);
        int result = dequeue(queue, &clientSocket);
        pthread_mutex_unlock(&queueLock);
        if (result != SUCCESS) {
            continue;
        }

        char request[SOCKET_READ_SIZE];
        int bytesRead = recv(clientSocket, request, sizeof(request), SEND_DEFAULT);

        if (bytesRead >= 1 && bytesRead < SOCKET_READ_SIZE) {
            request[bytesRead] = '\0';
            int sentResult = handleRequest(request, bytesRead, clientSocket, dbSocket);
            if (sentResult == ERROR) {
                log("{ Error sending response }\n");
            } else {
                log("{ Request handled }\n");
            }
        }
        close(clientSocket);
    }
}

typedef struct AcceptConnectionsArgs {
    Queue *queue;
    int serverSocket;
} AcceptConnectionsArgs;

void *acceptConnections(void *arg) {
    AcceptConnectionsArgs *args = (AcceptConnectionsArgs *)arg;
    int serverSocket = args->serverSocket;
    Queue *queue = args->queue;

    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);

    while (true) {
        log("{ Waiting to accept connection }\n");
        int clientSocket = accept(serverSocket, (SA *)&clientAddress, &clientAddressSize);
        if (clientSocket == -1) {
            log("{ Error accepting connection }\n");
            continue;
        }
        int result = -1, retries = 10;
        while (result != SUCCESS && retries > 0) {
            pthread_mutex_lock(&queueLock);
            result = enqueue(queue, clientSocket);
            pthread_mutex_unlock(&queueLock);
            retries--;
        }
        if (result != SUCCESS) {
            log("{ Error enqueuing client socket }\n");
            close(clientSocket);
        } else {
            log("{ Accepted connection on socket %d }\n", clientSocket);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <port> <database port>\n", argv[0]);
        return ERROR;
    }

    const int SERVER_PORT = atoi(argv[1]);
    const int DB_PORT = atoi(argv[2]);

    Queue queue;
    initQueue(&queue);

    log("{ connecting to db }\n");

    HandleConnectionArgs args = {&queue, DB_PORT};
    for (int i = 0; i < MAX_THREADS; i++) {
        int threadResult = pthread_create(&workerThreads[i], NULL, handleConnections, (void *)&args);
        raiseIfError(threadResult);
    }

    log("connected to db on port %d\n", DB_PORT);

    serverSocket = setupServer(SERVER_PORT, SERVER_BACKLOG);

    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);

    log("{ Server is running(%d) }\n", serverSocket);
    log("{ Listening on port %d }\n", SERVER_PORT);

    AcceptConnectionsArgs acceptConnectionsArgs = {&queue, serverSocket};
    int threadResult = pthread_create(&acceptConnectionsThread, NULL, acceptConnections, (void *)&acceptConnectionsArgs);
    raiseIfError(threadResult);

    pthread_join(acceptConnectionsThread, NULL);
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(workerThreads[i], NULL);
    }

    return cleanup(SIGTERM);
}
