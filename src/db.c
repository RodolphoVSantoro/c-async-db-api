#include "dbHandler.h"

int serverSocket;

// For profiling even if the server closes from a ctrl+c signal
void sigIntHandler(int signum) {
    printf("{ Caught signal %d }\n", signum);
    closeDBFiles();
    close(serverSocket);
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
    fd_set currentSockets, readySockets;

    // Initialize the set of active sockets
    FD_ZERO(&currentSockets);
    FD_SET(serverSocket, &currentSockets);

    while (true) {
        readySockets = currentSockets;

        // Wait for an activity on one of the sockets
        if (select(FD_SETSIZE, &readySockets, NULL, NULL, NULL) < 0) {
            printf("Select failed");
            return ERROR;
        }

        // Check all sockets for activity
        for (int socket = 0; socket < FD_SETSIZE; socket++) {
            if (FD_ISSET(socket, &readySockets)) {
                // Accept new connection
                if (socket == serverSocket) {
                    struct sockaddr_in clientAddress;
                    socklen_t clientAddressSize = sizeof(clientAddress);
                    int clientSocket = accept(serverSocket, (SA*)&clientAddress, &clientAddressSize);
                    FD_SET(clientSocket, &currentSockets);
                } else {
                    // Handle client request
                    int clientSocket = socket;
                    char request[SOCKET_READ_SIZE];
                    int bytesRead = recv(clientSocket, request, sizeof(request), SEND_DEFAULT);
                    bool shouldClose = true;

                    if (bytesRead >= 1 && bytesRead < SOCKET_READ_SIZE) {
                        request[bytesRead] = '\0';
                        int sentResult = handleRequest(request, bytesRead, clientSocket);
                        if (sentResult == ERROR) {
                            log("{ Error sending response }\n");
                        } else if (sentResult != END_CONNECTION) {
                            // Keep the connection open if the client didn't ask to close it
                            shouldClose = false;
                            log("{ Request handled }\n");
                        }
                    }

                    if (shouldClose) {
                        log("{ Client closed connection }\n");
                        close(clientSocket);
                        FD_CLR(clientSocket, &currentSockets);
                    }
                }
            }
        }
    }

    closeDBFiles();
    close(serverSocket);
    return EXIT_SUCCESS;
}
