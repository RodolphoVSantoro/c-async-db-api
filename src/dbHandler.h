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
// 1KB
#define SOCKET_READ_SIZE 1024
// 1KB
#define RESPONSE_SIZE 1024
// 1KB
#define RESPONSE_BODY_SIZE 1024

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
int handleRequest(char* request, int requestSize, char responseBuffer[], int* responseSize);

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
#define REQUEST_UNKNOWN_ASYNC(responseBuffer, responseReady) RESPOND_ASYNC(responseBuffer, requestUnknown, responseReady)

int handleRequest(char* request, int requestSize, char responseBuffer[], int* responseSize) {
    char responseReady;
#ifdef LOGGING
    char reqTime[DATE_SIZE];
    getCurrentTimeStr(reqTime);
#endif

    if (requestSize < 1) {
        log("{ %s - Empty request }\n", reqTime);
        REQUEST_UNKNOWN_ASYNC(responseBuffer, &responseReady);
        *responseSize = strlen(requestUnknown);
        return SUCCESS;
    }

    log("{ %s - Received:", reqTime);
    log(LOG_SEPARATOR);
    logRequest(request, requestSize);
    log(LOG_SEPARATOR);
    log("(%d bytes read) }\n", requestSize);

    // close connection request
    if (request[0] == '0') {
        strcpy(responseBuffer, "0 close");
        *responseSize = 8;
        return END_CONNECTION;
    }

    bool recognizedMethod = false;
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
        *responseSize = 1;
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
        *responseSize = 2;
        if (readResult == SUCCESS) {
            int userBytes = serializeUser(&user, &responseBuffer[2]);
            *responseSize += userBytes;
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
        log("{ Update user result: %d }\n", updateUserResult);
        responseBuffer[0] = (updateUserResult * -1) + '0';
        responseBuffer[1] = ' ';
        *responseSize = 2;

        if (updateUserResult == SUCCESS) {
            int userBytes = serializeUser(&user, &responseBuffer[2]);
            *responseSize += userBytes;
        }
    }

    // defer send response
    if (recognizedMethod) {
        return SUCCESS;
    }

    log("[ Method not allowed ]\n");
    REQUEST_UNKNOWN_ASYNC(responseBuffer, &responseReady);
    return SUCCESS;
}

#endif
