#include "httpHandler.h"

int serverSocket;

void cleanup() {
    close(serverSocket);
}

// For profiling even if the server closes from a ctrl+c signal
void sigIntHandler(int signum) {
    printf("{ Caught signal %d }\n", signum);
    cleanup();
    exit(EXIT_SUCCESS);
}

typedef enum SocketType {
    CLOSED,
    CLIENT,
    DB,
} SocketType;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <port> <database port>\n", argv[0]);
        return ERROR;
    }

    const int SERVER_PORT = atoi(argv[1]);
    const int DB_PORT = atoi(argv[2]);

    // log("connected to db on port %d\n", DB_PORT);

    serverSocket = setupServer(SERVER_PORT, SERVER_BACKLOG);
    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);
    log("{ Server is running(%d) }\n", serverSocket);
    log("{ Listening on port %d }\n", SERVER_PORT);
    log("{ FD_SETSIZE: %d }\n", FD_SETSIZE);

    // Set of socket descriptors
    fd_set currentReadSockets, currentWriteSockets;

    // set used on selects
    fd_set readSockets, writeSockets;

    // Initialize the set of active sockets
    FD_ZERO(&currentReadSockets);
    FD_SET(serverSocket, &currentReadSockets);

    FD_ZERO(&currentWriteSockets);

    char responseBuffers[FD_SETSIZE][RESPONSE_SIZE] = {0};
    char responseReady[FD_SETSIZE] = {0};
    HttpMethod requestMethod[FD_SETSIZE] = {0};
    int responseSize[FD_SETSIZE] = {0};

    char dbRequestBuffers[FD_SETSIZE][DB_REQUEST_SIZE] = {0};
    int dbRequestSize[FD_SETSIZE] = {0};
    char dbRequestReady[FD_SETSIZE] = {0};

    char dbResponseReady[FD_SETSIZE] = {0};

    int clientDbSocket[FD_SETSIZE];
    for (int i = 0; i < FD_SETSIZE; i++) {
        clientDbSocket[i] = -1;
    }

    int dbClientSocket[FD_SETSIZE];
    for (int i = 0; i < FD_SETSIZE; i++) {
        dbClientSocket[i] = -1;
    }

    SocketType socketType[FD_SETSIZE];

    char shouldClose[FD_SETSIZE] = {0};

    int selectResult;

    while (true) {
        readSockets = currentReadSockets;
        writeSockets = currentWriteSockets;

        // Wait for an activity on one of the sockets
        selectResult = select(FD_SETSIZE, &readSockets, &writeSockets, NULL, NULL);
        if (selectResult < 0) {
            printf("Select failed");
            return ERROR;
        }

        // Accept new connection
        if (FD_ISSET(serverSocket, &readSockets)) {
            struct sockaddr_in clientAddress;
            socklen_t clientAddressSize = sizeof(clientAddress);
            int clientSocket = accept(serverSocket, (SA*)&clientAddress, &clientAddressSize);
            FD_SET(clientSocket, &currentReadSockets);
            socketType[clientSocket] = CLIENT;
        }

        // Check all sockets for activity
        for (int socket = 0; socket < FD_SETSIZE; socket++) {
            if (selectResult == 0) {
                break;
            }
            if (socketType[socket] == CLOSED) {
                continue;
            }
            int clientSocket = -1, dbSocket = -1;
            if (socketType[socket] == CLIENT) {
                clientSocket = socket;
                dbSocket = clientDbSocket[clientSocket];
            }
            if (socketType[socket] == DB) {
                dbSocket = socket;
                clientSocket = dbClientSocket[dbSocket];
            }

            // read client recv buffer and serialize request to db
            bool shouldReadRequestFromClient = dbRequestReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldReadRequestFromClient && FD_ISSET(clientSocket, &readSockets)) {
                if (clientDbSocket[clientSocket] == -1) {
                    dbSocket = clientDbSocket[clientSocket] = connectToDb(DB_PORT);
                    socketType[dbSocket] = DB;
                    dbClientSocket[dbSocket] = clientSocket;
                }
                char request[SOCKET_READ_SIZE];
                int bytesRead = recv(clientSocket, request, sizeof(request), SEND_DEFAULT);

                if (bytesRead >= 1 && bytesRead < SOCKET_READ_SIZE && shouldClose[clientSocket] == 0) {
                    request[bytesRead] = '\0';
                    int serializeDbResult = deserializeClientRequest(
                        request,
                        bytesRead,
                        dbRequestBuffers[clientSocket],
                        &dbRequestSize[clientSocket],
                        &requestMethod[clientSocket],
                        responseBuffers[clientSocket],
                        &responseReady[clientSocket]);

                    FD_CLR(clientSocket, &currentReadSockets);

                    // Early return in case response can be determined without db request
                    if (serializeDbResult == ERROR) {
                        log("{ Error reading request }\n");
                        dbRequestReady[clientSocket] = 1;
                        dbResponseReady[clientSocket] = 1;
                        FD_SET(clientSocket, &currentWriteSockets);
                        responseSize[clientSocket] = strlen(responseBuffers[clientSocket]);
                    } else {
                        log("{ Request read on %d [ %s ] }\n", clientSocket, request);
                        FD_SET(dbSocket, &currentWriteSockets);
                        dbRequestReady[clientSocket] = 1;
                        dbRequestSize[clientSocket] = strlen(dbRequestBuffers[clientSocket]);
                    }
                    continue;
                }
            }

            // send request to db
            bool shouldSendRequestToDb = dbRequestReady[clientSocket] == 1 && dbResponseReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldSendRequestToDb && FD_ISSET(dbSocket, &writeSockets)) {
                int sendDbRequestResult = send(dbSocket, dbRequestBuffers[clientSocket], dbRequestSize[clientSocket], SEND_DEFAULT);
                if (sendDbRequestResult == ERROR) {
                    log("{ Error sending request to db %d, %d }\n", sendDbRequestResult, errno);
                }
                log("{ Sent request to db (%d) }\n", sendDbRequestResult);
                dbResponseReady[clientSocket] = 1;
                FD_CLR(dbSocket, &currentWriteSockets);
                FD_SET(dbSocket, &currentReadSockets);
                continue;
            }

            // read response from db and serialize response to client
            bool shouldReadResponseFromDb = dbResponseReady[clientSocket] == 1 && responseReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldReadResponseFromDb && FD_ISSET(dbSocket, &readSockets)) {
                char dbResponse[DB_RESPONSE_SIZE];
                int bytesRead = recv(dbSocket, dbResponse, sizeof(dbResponse), SEND_DEFAULT);

                if (bytesRead >= 1 && bytesRead < DB_RESPONSE_SIZE) {
                    log("{ read bytes %d for %d }\n", bytesRead, clientSocket);
                    dbResponse[bytesRead] = '\0';
                    log("[ ");
                    for (int i = 0; i < bytesRead; i++) {
                        log("'%c'", dbResponse[i]);
                    }
                    log("]\n");
                    int serializeResponseResult = serializeClientResponse(
                        dbResponse,
                        bytesRead,
                        requestMethod[clientSocket],
                        responseBuffers[clientSocket],
                        &responseReady[clientSocket]);
                    if (serializeResponseResult == ERROR) {
                        log("{ Error reading response from db }\n");
                        shouldClose[clientSocket] = 1;
                    } else {
                        log("{ Db response read }\n");
                        responseReady[clientSocket] = 1;
                        responseSize[clientSocket] = strlen(responseBuffers[clientSocket]);
                    }

                    FD_CLR(dbSocket, &currentReadSockets);
                    FD_SET(clientSocket, &currentWriteSockets);
                } else {
                    printf("Receive returned %d\n", bytesRead);
                    exit(ERROR);
                }
                continue;
            }

            // send response to client and set the connection to close
            bool shouldSendResponseToClient = responseReady[clientSocket] == 1 && shouldClose[clientSocket] == 0;
            if (shouldSendResponseToClient && FD_ISSET(clientSocket, &writeSockets)) {
                int sendResult = send(clientSocket, responseBuffers[clientSocket], responseSize[clientSocket], SEND_DEFAULT);
                if (sendResult == ERROR) {
                    log("{ Error sending response }\n");
                } else {
                    log("{ Sent response [ %s ] on socket %d }\n", responseBuffers[clientSocket], clientSocket);
                }
                shouldClose[clientSocket] = 1;
                FD_CLR(clientSocket, &currentWriteSockets);
                continue;
            }

            // close connection with client if it's done
            if (shouldClose[clientSocket] == 1) {
                responseReady[clientSocket] = 0;
                dbRequestReady[clientSocket] = 0;
                dbResponseReady[clientSocket] = 0;
                shouldClose[clientSocket] = 0;
                socketType[clientSocket] = CLOSED;

                if (dbSocket != -1) {
                    socketType[dbSocket] = CLOSED;
                    dbClientSocket[dbSocket] = -1;
                    close(dbSocket);
                    clientDbSocket[clientSocket] = -1;
                }
                close(clientSocket);
                log("{ Closed client connection %d }\n", clientSocket);
            }
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}
