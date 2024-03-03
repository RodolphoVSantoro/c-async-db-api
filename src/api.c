#include "httpHandler.h"

int serverSocket, dbSocket;

// For profiling even if the server closes from a ctrl+c signal
void sigIntHandler(int signum) {
    printf("{ Caught signal %d }\n", signum);
    close(dbSocket);
    close(serverSocket);
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <port> <database port>\n", argv[0]);
        return ERROR;
    }

    const int SERVER_PORT = atoi(argv[1]);
    const int DB_PORT = atoi(argv[2]);

    log("{ connecting to db }\n");
    dbSocket = connectToDb(DB_PORT);
    if (dbSocket == ERROR) {
        log("{ Error connecting to db }\n");
        return ERROR;
    }
    log("connected to db on port %d\n", DB_PORT);

    serverSocket = setupServer(SERVER_PORT, SERVER_BACKLOG);
    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);
    log("{ Server is running(%d) }\n", serverSocket);
    log("{ Listening on port %d }\n", SERVER_PORT);
    log("{ FD_SETSIZE: %d }\n", FD_SETSIZE);

    // Set of socket descriptors
    fd_set currentReadSockets, currentWriteSockets, readSockets, writeSockets;

    // Initialize the set of active sockets
    FD_ZERO(&currentReadSockets);
    FD_SET(serverSocket, &currentReadSockets);

    FD_ZERO(&currentWriteSockets);
    FD_SET(dbSocket, &currentWriteSockets);
    FD_SET(dbSocket, &currentReadSockets);

    char responseBuffers[FD_SETSIZE][RESPONSE_SIZE] = {0};
    char responseReady[FD_SETSIZE] = {0};
    HttpMethod requestMethod[FD_SETSIZE] = {0};
    int responseSize[FD_SETSIZE] = {0};

    char dbRequestBuffers[FD_SETSIZE][DB_REQUEST_SIZE] = {0};
    int dbRequestSize[FD_SETSIZE] = {0};
    char dbRequestReady[FD_SETSIZE] = {0};

    char dbResponseReady[FD_SETSIZE] = {0};

    char shouldClose[FD_SETSIZE] = {0};

    while (true) {
        readSockets = currentReadSockets;
        writeSockets = currentWriteSockets;

        // Wait for an activity on one of the sockets
        if (select(FD_SETSIZE, &readSockets, &writeSockets, NULL, NULL) < 0) {
            printf("Select failed");
            return ERROR;
        }

        // Accept new connection
        if (FD_ISSET(serverSocket, &readSockets)) {
            struct sockaddr_in clientAddress;
            socklen_t clientAddressSize = sizeof(clientAddress);
            int clientSocket = accept(serverSocket, (SA*)&clientAddress, &clientAddressSize);
            FD_SET(clientSocket, &currentReadSockets);
        }

        // Check all sockets for activity
        for (int clientSocket = 0; clientSocket < FD_SETSIZE; clientSocket++) {
            if (clientSocket == serverSocket || clientSocket == dbSocket) {
                continue;
            }

            // read client recv buffer and serialize request to db
            bool shouldReadRequestFromClient = dbRequestReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldReadRequestFromClient && FD_ISSET(clientSocket, &readSockets)) {
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
                    if (serializeDbResult == ERROR) {
                        log("{ Error reading request }\n");
                        shouldClose[clientSocket] = 1;
                    } else {
                        log("{ Request read }\n");
                        // Early return in case response can be determined without db request
                        FD_CLR(clientSocket, &currentReadSockets);
                        if (responseReady[clientSocket] == 1) {
                            dbRequestReady[clientSocket] = 1;
                            dbResponseReady[clientSocket] = 1;
                            responseSize[clientSocket] = strlen(responseBuffers[clientSocket]);
                        } else {
                            dbRequestReady[clientSocket] = 1;
                            dbRequestSize[clientSocket] = strlen(dbRequestBuffers[clientSocket]);
                        }
                    }
                }
                continue;
            }

            // send request to db
            bool shouldSendRequestToDb = dbRequestReady[clientSocket] == 1 && dbResponseReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldSendRequestToDb && FD_ISSET(dbSocket, &writeSockets)) {
                int sendDbRequestResult = send(dbSocket, dbRequestBuffers[clientSocket], dbRequestSize[clientSocket], SEND_DEFAULT);
                if (sendDbRequestResult == ERROR) {
                    log("{ Error sending request to db }\n");
                }
                log("{ Sent request to db }");
                dbResponseReady[clientSocket] = 1;
                continue;
            }

            // read response from db and serialize response to client
            bool shouldReadResponseFromDb = dbResponseReady[clientSocket] == 1 && responseReady[clientSocket] == 0 && shouldClose[clientSocket] == 0;
            if (shouldReadResponseFromDb && FD_ISSET(dbSocket, &readSockets)) {
                char dbResponse[DB_RESPONSE_SIZE];
                int bytesRead = recv(dbSocket, dbResponse, sizeof(dbResponse), SEND_DEFAULT);

                if (bytesRead >= 1 && bytesRead < DB_RESPONSE_SIZE) {
                    dbResponse[bytesRead] = '\0';
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

                    FD_SET(clientSocket, &currentWriteSockets);
                }
                continue;
            }

            // send response to client and set the connection to close
            bool shouldSendResponseToClient = responseReady[clientSocket] == 1 && shouldClose[clientSocket] == 0;
            if (FD_ISSET(clientSocket, &writeSockets) && shouldSendResponseToClient) {
                int sendResult = send(clientSocket, responseBuffers[clientSocket], responseSize[clientSocket], SEND_DEFAULT);
                if (sendResult == ERROR) {
                    log("{ Error sending response }\n");
                } else {
                    log("{ Sent response [ %s ] }\n", responseBuffers[clientSocket]);
                }
                shouldClose[clientSocket] = 1;
                FD_CLR(clientSocket, &currentWriteSockets);
            }

            // close connection with client if it's done
            if (shouldClose[clientSocket] == 1) {
                responseReady[clientSocket] = 0;
                dbRequestReady[clientSocket] = 0;
                dbResponseReady[clientSocket] = 0;
                shouldClose[clientSocket] = 0;
                close(clientSocket);
                log("{ Closed client connection }\n");
            }
        }
    }

    close(dbSocket);
    close(serverSocket);
    return EXIT_SUCCESS;
}
