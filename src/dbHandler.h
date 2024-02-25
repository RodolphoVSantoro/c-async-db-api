#ifndef DB_HANDLER_H
#define DB_HANDLER_H

// Header file for the db socket handler
// Handles the server db socket requests and responses
// Calls the file database functions to read and write to the files

#include "dbFiles.h"

// server port
// #define SERVER_PORT 9999
// max connections waiting to be accepted
#define SERVER_BACKLOG 1000
// 8KB
#define SOCKET_READ_SIZE 8 * 1024
// 16KB
#define RESPONSE_SIZE 16 * 1024
// 8KB
#define RESPONSE_BODY_SIZE 8 * 1024
// 4KB
#define RESPONSE_BODY_TRANSACTIONS_SIZE 4 * 1024

#ifdef LOGGING
#define logRequest(request, requestSize)    \
    for (int i = 0; i < requestSize; i++) { \
        printf("'%d'", request[i]);         \
    }                                       \
    printf("\n");
#else
#define logRequest(request, requestSize) (void)0
#endif

#define LOG_SEPARATOR "\n----------------------------------------------\n"

// socket send default flag
#define SEND_DEFAULT 0
#define PROTOCOL_DEFAULT 0

// socket results
#define END_CONNECTION 1

// Startup server socket on the given port, with the max number of connections waiting to be accepted set to backlog
// Crash the program if the socket creation or binding fails
int setupServer(short port, int backlog);

// Handles the request and sends the response to the clientSocket
// Returns END_CONNECTION if the client requests to close the connection
// Returns SUCCESS if the request was successful
// Returns ERROR if the request was not successful
int handleRequest(char* request, int requestSize, int clientSocket);

int setupServer(short port, int backlog) {
    int serverSocket;
    check((serverSocket = socket(AF_INET, SOCK_STREAM, PROTOCOL_DEFAULT)), "Failed to create socket");

    SA_IN serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    //  To avoid "Address already in use" error when restarting the server because of the TIME_WAIT state
    // https://handsonnetworkprogramming.com/articles/bind-error-98-eaddrinuse-10048-wsaeaddrinuse-address-already-in-use/
    int yes = 1;
    check(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)), "Failed to set socket options");

    check(bind(serverSocket, (SA*)&serverAddress, sizeof(serverAddress)), "Failed to bind socket");
    check(listen(serverSocket, backlog), "Failed to listen on socket");

    return serverSocket;
}

const char requestUnknown[] = "1 - Unknown request\n\n";
#define REQUEST_UNKNOWN(clientSocket) STATIC_RESPONSE(clientSocket, requestUnknown)

int handleRequest(char* request, int requestSize, int clientSocket) {
    char reqTime[DATE_SIZE];
    getCurrentTimeStr(reqTime);

    if (requestSize < 1) {
        log("{ %s - Empty request }\n", reqTime);
        return REQUEST_UNKNOWN(clientSocket);
    }

    log("{ %s - Received:", reqTime);
    log(LOG_SEPARATOR);
    logRequest(request, requestSize);
    log(LOG_SEPARATOR);
    log("(%d bytes read) }\n", requestSize);

    // close connection request
    if (request[0] == '0') {
        send(clientSocket, "0 close", 8, SEND_DEFAULT);
        return END_CONNECTION;
    }

    bool recognizedMethod = false;
    char responseBuffer[DB_RESPONSE_SIZE];
    int bufferLen = 0;
    if (request[0] == 'c') {
        recognizedMethod = true;
        log("[ Create user request ]\n");

        int id = request[2] - '0';
        int limit = fromBin(&request[4]);
        User user;
        user.total = 0;
        user.nTransactions = 0;
        user.oldestTransaction = 0;
        user.id = id;
        user.limit = limit;
        int writeUserResult = writeUser(&user);

        responseBuffer[0] = (writeUserResult * -1) + '0';
        bufferLen = 1;
    }

    if (request[0] == 'r') {
        recognizedMethod = true;
        int id = request[2] - '0';

        log("[ Read user request ]\n");
        User user;
        int readResult = readUser(&user, id);
        log("{ Read result: %d }\n", readResult);
        log("{ User limit: %d }\n", user.limit);
        log("{ User total: %d }\n", user.total);
        log("{ User nTransactions: %d }\n", user.nTransactions);
        log("{ User oldestTransaction: %d }\n", user.oldestTransaction);

        responseBuffer[0] = (readResult * -1) + '0';
        responseBuffer[1] = ' ';
        bufferLen = 2;
        if (readResult == SUCCESS) {
            int userBytes = serializeUser(&user, &responseBuffer[2]);
            bufferLen += userBytes;
        }
    }

    if (request[0] == 'u') {
        recognizedMethod = true;
        log("[ Update user request ]\n");

        User user;
        int id = request[2] - '0';
        Transaction transaction;
        transaction.tipo = request[4];
        transaction.valor = fromBin(&request[6]);
        strncpy(transaction.descricao, &request[11], DESCRIPTION_SIZE - 1);
        getCurrentTimeStr(transaction.realizada_em);

        int updateUserResult = updateUserWithTransaction(id, &transaction, &user);
        responseBuffer[0] = (updateUserResult * -1) + '0';
        responseBuffer[1] = ' ';
        bufferLen = 2;

        if (updateUserResult == SUCCESS) {
            int userBytes = serializeUser(&user, &responseBuffer[2]);
            bufferLen += userBytes;
        }
    }

    // send response
    if (recognizedMethod) {
        send(clientSocket, responseBuffer, bufferLen, SEND_DEFAULT);
        return SUCCESS;
    }

    log("[ Method not allowed ]\n");
    return REQUEST_UNKNOWN(clientSocket);
}

#endif
