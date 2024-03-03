#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

// Header file for the http handler
// Handles the http requests and responses
// Handles deserialization and serialization of the requests and responses
// Calls the database client functions to call the server db socket

#include "dbClient.h"

// server port
// #define SERVER_PORT 9999
// max connections waiting to be accepted
#define SERVER_BACKLOG 2000
// 8KB
#define SOCKET_READ_SIZE 8 * 1024
// 16KB
#define RESPONSE_SIZE 4 * 1024
// 8KB
#define RESPONSE_BODY_SIZE 8 * 1024
// 256B
#define RESPONSE_BODY_TRANSACTIONS_SIZE 256

#define LOG_SEPARATOR "\n----------------------------------------------\n"

// socket send default flag
#define SEND_DEFAULT 0
#define PROTOCOL_DEFAULT 0

#define DESERIALIZE_HTTP_ERROR 1

typedef enum HttpMethod {
    UNSET = 0,
    GET = 1,
    POST = 2,
} HttpMethod;

// Startup server socket on the given port, with the max number of connections waiting to be accepted set to backlog
// Crash the program if the socket creation or binding fails
int setupServer(short port, int backlog);

// Deserializes db response, serializes client response
int serializeClientResponse(char* dbResponse, int dbResponseSize, HttpMethod requestMethod, char responseBuffer[], char* responseReady);

// Deserializes client request, serializes db request
// Returns ERROR if the request has a problem
// Returns SUCCESS if the request is serialized successfully
int deserializeClientRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, HttpMethod* requestMethod, char responseBuffer[], char* responseReady);

// Deserializes client request, serializes db request
int deserializeGetRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, char responseBuffer[], char* responseReady);
// Assuming the request is "GET /clientes/1/..." id is on the 14th position
// Returns ERROR if the request is invalid
// Returns the id if the request is valid
int getIdFromGETRequest(const char* request, int requestLength);
// Serializes GET bank statement response into json and writes it to response
void serializeGetResponse(User* user, char* response);

// Deserializes client request, serializes db request
int deserializePostRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, char responseBuffer[], char* responseReady);
// Assuming the request is "POST /clientes/1/..." id is on the 15th position
// Returns ERROR if the request is invalid
// Returns the id if the request is valid
int getIdFromPOSTRequest(const char* request, int requestLength);
// Get transaction from body
// returns ERROR if the body it fails to parse the body
// returns SUCCESS if it parses the body successfully
// Sets the transaction variable with the parsed values
// Sets the transaction.realizada_em with the current time
int getTransactionFromBody(char* request, Transaction* transaction);
// Serializes POST transaction response into json and writes it to response
void serializePostResponse(User* user, char* response);

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

int deserializeClientRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, HttpMethod* requestMethod, char responseBuffer[], char* responseReady) {
#ifdef LOGGING
    char reqTime[DATE_SIZE];
    getCurrentTimeStr(reqTime);
#endif

    log("{ %s - Received:", reqTime);
    log(LOG_SEPARATOR);
    log("[%s]", request);
    log(LOG_SEPARATOR);
    log("(%d bytes read) }\n", requestSize);

    // "GET" alone has 3 bytes, so we need at least 4 bytes to consume a request
    if (requestSize < 4) {
        log("[ Unprocessable Entity ]\n");
        UNPROCESSABLE_ENTITY_ASYNC(responseBuffer, responseReady);
        return SUCCESS;
    }

    if (partialEqual(request, GET_METHOD, GET_METHOD_LENGTH)) {
        *requestMethod = GET;
        return deserializeGetRequest(request, requestSize, dbRequestBuffer, dbRequestSize, responseBuffer, responseReady);
    }
    if (partialEqual(request, POST_METHOD, POST_METHOD_LENGTH)) {
        *requestMethod = POST;
        return deserializePostRequest(request, requestSize, dbRequestBuffer, dbRequestSize, responseBuffer, responseReady);
    }

    log("[ Method not allowed ]\n");
    METHOD_NOT_ALLOWED_ASYNC(responseBuffer, responseReady);
    return SUCCESS;
}

// Deserializes client request, serializes db request
int deserializeGetRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, char responseBuffer[], char* responseReady) {
    // get id from request path
    int id = getIdFromGETRequest(request, requestSize);
    if (id == ERROR) {
        log("[ NOT_FOUND - id over 9 ]\n");
        NOT_FOUND_ASYNC(responseBuffer, responseReady);
        return ERROR;
    }

    dbRequestBuffer[0] = 'r';
    dbRequestBuffer[1] = ' ';
    if (id > 9) {
        return ERROR;
    }
    dbRequestBuffer[2] = id + '0';
    dbRequestBuffer[3] = '\0';

    *dbRequestSize = 4;

    return SUCCESS;
}

// Deserializes client request, serializes db request
int deserializePostRequest(char* request, int requestSize, char* dbRequestBuffer, int* dbRequestSize, char responseBuffer[], char* responseReady) {
    // get id from request path
    int id = getIdFromPOSTRequest(request, requestSize);
    if (id == ERROR) {
        log("[ NOT_FOUND - id over 9 ]\n");
        NOT_FOUND_ASYNC(responseBuffer, responseReady);
        return ERROR;
    }

    Transaction transaction;
    int parseResult = getTransactionFromBody(request, &transaction);
    if (parseResult == ERROR) {
        log("[ Unprocessable Entity - Failed to get body ]\n");
        UNPROCESSABLE_ENTITY_ASYNC(responseBuffer, responseReady);
        return ERROR;
    }

    // 'u' id(binNum) tipo('c' ou 'd') valor(binNum) descricao(char[DESCRIPTION_SIZE])
    dbRequestBuffer[0] = 'u';
    dbRequestBuffer[1] = ' ';

    char charId = id + '0';
    dbRequestBuffer[2] = charId;
    dbRequestBuffer[3] = ' ';
    dbRequestBuffer[4] = transaction.tipo;
    dbRequestBuffer[5] = ' ';
    toBin(transaction.valor, &dbRequestBuffer[6]);
    dbRequestBuffer[10] = ' ';
    strcpy(&dbRequestBuffer[11], transaction.descricao);

    *dbRequestSize = 11 + DESCRIPTION_SIZE;

    return SUCCESS;
}

// Deserializes db response, serializes client response
int serializeClientResponse(char* dbResponse, int dbResponseSize, HttpMethod requestMethod, char responseBuffer[], char* responseReady) {
    if (dbResponseSize == -1) {
        log("[ Internal Server Error - Reading response from db ]\n");
        INTERNAL_SERVER_ERROR_ASYNC(responseBuffer, responseReady);
        return ERROR;
    }

    if (dbResponse[0] != '0') {
        if (requestMethod == GET) {
            NOT_FOUND_ASYNC(responseBuffer, responseReady);
        } else {
            int transactionResult = -(dbResponse[0] - '0');
            if (transactionResult == ERROR) {
                log("[ Internal Server Error - Locking file ]\n");
                INTERNAL_SERVER_ERROR_ASYNC(responseBuffer, responseReady);
            } else if (transactionResult == FILE_NOT_FOUND) {
                log("[ Not Found - User file ]\n");
                NOT_FOUND_ASYNC(responseBuffer, responseReady);
            } else if (transactionResult == LIMIT_EXCEEDED_ERROR || transactionResult == INVALID_TIPO_ERROR) {
                log("[ Unprocessable entity - LIMIT OR TIPO ]\n");
                UNPROCESSABLE_ENTITY_ASYNC(responseBuffer, responseReady);
            } else {
                log("[ Internal Server Error - Unknown error %d]\n", transactionResult);
                INTERNAL_SERVER_ERROR_ASYNC(responseBuffer, responseReady);
            }
        }
        return SUCCESS;
    }

    User user;
    deserializeUser(&dbResponse[2], &user);

    if (requestMethod == GET) {
        serializeGetResponse(&user, responseBuffer);
    }
    if (requestMethod == POST) {
        serializePostResponse(&user, responseBuffer);
    }
    log("respond: [ %s ]\n", responseBuffer);
    return SUCCESS;
}

int getIdFromGETRequest(const char* request, int requestLength) {
    if (requestLength < 15) {
        return ERROR;
    }
    if (request[13] != '/' || request[15] != '/') {
        return ERROR;
    }
    if (request[14] < '0' || request[14] > '9') {
        return ERROR;
    }
    return request[14] - '0';
}

void serializeOrderedTransactions(User* user, char* body) {
    if (user->nTransactions == 0) {
        return;
    }
    char transactionData[RESPONSE_BODY_TRANSACTIONS_SIZE];

    int i = user->oldestTransaction;
    i = (i - 1 + user->nTransactions) % user->nTransactions;
    for (int j = 0; j < user->nTransactions; j++) {
        Transaction transaction = user->transactions[i];
        const char* transactionTemplate = "{\"valor\":%d,\"tipo\":\"%c\",\"descricao\":\"%s\",\"realizada_em\":\"%s\"},";
        sprintf(transactionData,
                transactionTemplate,
                transaction.valor, transaction.tipo, transaction.descricao, transaction.realizada_em);

        strcat(body, transactionData);
        i = (i - 1 + user->nTransactions) % user->nTransactions;
    }
    int length = strlen(body);
    body[length - 1] = '\0';
}

void serializeGetResponse(User* user, char* response) {
    char body[RESPONSE_BODY_SIZE] = "";
    char dateTime[DATE_SIZE];

    // First part of the response
    getCurrentTimeStr(dateTime);
    const char* userDataTemplate = "{\"saldo\":{\"total\":%d,\"data_extrato\":\"%s\",\"limite\":%d},\"ultimas_transacoes\":[";
    sprintf(body,
            userDataTemplate,
            user->total, dateTime, user->limit);

    serializeOrderedTransactions(user, body);

    // Close the array and the outermost object
    strcat(body, "]}");

    // Write the http response using a success template, with a body
    sprintf(response, successResponseJsonTemplate, body);
}

int getIdFromPOSTRequest(const char* request, int requestLength) {
    if (requestLength < 16) {
        return ERROR;
    }
    if (request[14] != '/' || request[16] != '/') {
        return ERROR;
    }
    if (request[15] < '0' || request[15] > '9') {
        return ERROR;
    }
    return request[15] - '0';
}

int getValorFromBody(char* str) {
    const int maxDigits = 20;
    for (int i = 0; i < maxDigits; i++) {
        if (str[i] == ',') {
            break;
        }
        if ((str[i] < '0' || str[i] > '9') && str[i] != ' ') {
            return ERROR;
        }
    }
    return atoi(str);
}

int getTransactionFromBody(char* request, Transaction* transaction) {
    // Find body in request
    char* body = strstr(request, "{");
    errIfNull(body);

    // Find valor key in body
    char* valor = strstr(body, "valor");
    errIfNull(valor);
    valor = strstr(valor, ":");
    errIfNull(valor);
    valor = &valor[1];
    transaction->valor = getValorFromBody(valor);
    raiseIfError(transaction->valor);

    // Find tipo key in body
    char* tipo = strstr(body, "tipo");
    errIfNull(tipo);
    tipo = strstr(tipo, ":");
    errIfNull(tipo);
    tipo = strstr(tipo, "\"");
    errIfNull(tipo);
    transaction->tipo = tipo[1];

    // Find descricao key in body
    char* descricaoStart = strstr(body, "descricao");
    errIfNull(descricaoStart);
    descricaoStart = strstr(descricaoStart, ":");
    errIfNull(descricaoStart);
    // Find the quote before the value
    descricaoStart = strstr(descricaoStart, "\"");
    errIfNull(descricaoStart);
    descricaoStart++;
    // Find the quote after the value
    char* descricaoEnd = strstr(descricaoStart, "\"");
    errIfNull(descricaoEnd);
    // Copy the value to the transaction
    char descricao[DESCRIPTION_SIZE];
    int length = descricaoEnd - descricaoStart;
    if (length > 10 || length < 1) {
        return ERROR;
    }
    strncpy(descricao, descricaoStart, length);
    descricao[length] = '\0';
    strcpy(transaction->descricao, descricao);

    // Set the transaction realizada_em to the current time
    getCurrentTimeStr(transaction->realizada_em);

    return SUCCESS;
}

void serializePostResponse(User* user, char* response) {
    const char* postResponseTemplate = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n{\"limite\":%d, \"saldo\":%d}";
    sprintf(response, postResponseTemplate, user->limit, user->total);
}
#endif