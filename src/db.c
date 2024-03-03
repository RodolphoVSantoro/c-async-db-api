#include "dbHandler.h"

int serverSocket;

char* responseBuffers[FD_SETSIZE];
char* requestBuffers[FD_SETSIZE];

void cleanup() {
    for (int i = 0; i < FD_SETSIZE; i++) {
        free(responseBuffers[i]);
        free(requestBuffers[i]);
    }
    closeDBFiles();
    close(serverSocket);
}

// For profiling even if the server closes from a ctrl+c signal
void sigIntHandler(int signum) {
    printf("{ Caught signal %d }\n", signum);
    cleanup();
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return ERROR;
    }

    const int SERVER_PORT = atoi(argv[1]);

    log("{ Starting database on PORT %d }\n", SERVER_PORT);

#ifdef RESET_DB
    log("{ Creating data folder }\n");
    int createFolderResult = system("mkdir -p data");
    raiseIfError(createFolderResult);
    log("{ Resetting database }\n");
    int resetDbResult = initDb();
    raiseIfError(resetDbResult);
    log("{ Database reset successfully }\n");
#endif

    log("{ Starting up server }\n");
    serverSocket = setupServer(SERVER_PORT, SERVER_BACKLOG);
    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);
    log("{ Server is running(%d) }\n", serverSocket);
    log("{ Listening on port %d }\n", SERVER_PORT);
    log("{ FD_SETSIZE: %d }\n", FD_SETSIZE);

    // Set of socket descriptors
    fd_set currentReadSockets, currentWriteSockets, readyReadSockets, readyWriteSockets;

    // Initialize the set of active sockets
    FD_ZERO(&currentReadSockets);
    FD_SET(serverSocket, &currentReadSockets);

    FD_ZERO(&currentWriteSockets);

    for (int i = 0; i < FD_SETSIZE; i++) {
        responseBuffers[i] = (char*)malloc(sizeof(char) * RESPONSE_SIZE);
        if (responseBuffers[i] == NULL) {
            printf("Failed to allocate memory for responseBuffers[%d]\n", i);
            return ERROR;
        }
        memset(responseBuffers[i], 0, RESPONSE_SIZE * sizeof(char));
        requestBuffers[i] = (char*)malloc(sizeof(char) * SOCKET_READ_SIZE);
        if (requestBuffers[i] == NULL) {
            printf("Failed to allocate memory for responseBuffers[%d]\n", i);
            return ERROR;
        }
        memset(requestBuffers[i], 0, SOCKET_READ_SIZE * sizeof(char));
    }

    int responseSize[FD_SETSIZE] = {0};
    char requestRead[FD_SETSIZE] = {0};
    int requestSize[FD_SETSIZE] = {0};
    bool shouldClose[FD_SETSIZE] = {0};

    while (true) {
        readyReadSockets = currentReadSockets;
        readyWriteSockets = currentWriteSockets;

        // Wait for an activity on one of the sockets
        int selectResult = select(FD_SETSIZE, &readyReadSockets, &readyWriteSockets, NULL, NULL);
        if (selectResult < 0) {
            printf("Select failed %d, %d\n", selectResult, errno);
            return ERROR;
        }

        if (FD_ISSET(serverSocket, &readyReadSockets)) {
            struct sockaddr_in clientAddress;
            socklen_t clientAddressSize = sizeof(clientAddress);
            int clientSocket = accept(serverSocket, (SA*)&clientAddress, &clientAddressSize);
            FD_SET(clientSocket, &currentReadSockets);
        }

        // Check all sockets for activity
        for (int socket = 0; socket < FD_SETSIZE; socket++) {
            if (socket == serverSocket) {
                continue;
            }
            int clientSocket = socket;
            bool readRequest = requestRead[clientSocket];
            if (readRequest == 0 && shouldClose[clientSocket] == false && FD_ISSET(clientSocket, &readyReadSockets)) {
                // Read client request
                int bytesRead = recv(clientSocket, requestBuffers[clientSocket], sizeof(requestBuffers[clientSocket]), SEND_DEFAULT);
                log("{ Read request from client %d }\n", clientSocket);
                logRequest(requestBuffers[clientSocket], bytesRead);
                FD_CLR(clientSocket, &currentReadSockets);
                if (bytesRead >= 1 && bytesRead < SOCKET_READ_SIZE) {
                    requestBuffers[clientSocket][bytesRead] = '\0';
                    requestSize[clientSocket] = bytesRead;
                    // client requested to close connection
                    if (requestBuffers[clientSocket][0] == '0') {
                        shouldClose[clientSocket] = true;
                    } else {
                        requestRead[clientSocket] = 1;
                        int result = handleRequest(
                            requestBuffers[clientSocket],
                            requestSize[clientSocket],
                            responseBuffers[clientSocket],
                            &responseSize[clientSocket]);
                        if (result == SUCCESS) {
                            FD_SET(clientSocket, &currentWriteSockets);
                            continue;
                        } else {
                            log("{ Client closed connection }\n");
                            shouldClose[clientSocket] = true;
                        }
                    }
                } else if (bytesRead == 0) {
                    log("{ Client closed }\n");
                    shouldClose[clientSocket] = true;
                }
                continue;
            }
            if (readRequest == 1 && shouldClose[clientSocket] == false && FD_ISSET(clientSocket, &readyWriteSockets)) {
                int sentResult = send(clientSocket, responseBuffers[clientSocket], responseSize[clientSocket], SEND_DEFAULT);
                if (sentResult == ERROR) {
                    log("{ Error sending response }\n");
                } else {
                    log("{ Request handled }\n");
                    log("{ Sent: ");
                    logRequest(responseBuffers[clientSocket], responseSize[clientSocket]);
                    log("}");
                }
                requestRead[clientSocket] = 0;
                FD_CLR(clientSocket, &currentWriteSockets);
                shouldClose[clientSocket] = true;
                continue;
            }

            if (shouldClose[clientSocket]) {
                shouldClose[clientSocket] = false;
                log("{ Client closed connection }\n");
                close(clientSocket);
            }
        }
    }
    cleanup();
    return EXIT_SUCCESS;
}
