#ifndef HELPERS_H
#define HELPERS_H

// Header file for helper functions and constants
// Includes the countless standard libraries used in C socket programming

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Comment out to enable logging
// #define LOGGING 1

// Debug flags
#ifdef LOGGING
#define log(message, ...) printf(message, ##__VA_ARGS__)
#else
#define log(message, ...) (void)0
#endif

// Custom error codes
#define ERROR -1
#define SUCCESS 0
#define FILE_NOT_FOUND -2
#define LIMIT_EXCEEDED_ERROR -3
#define INVALID_TIPO_ERROR -4

// Return error if pointer is NULL
#define errIfNull(pointer) \
    if (pointer == NULL) { \
        return ERROR;      \
    }

// Return error if the result is an error
#define raiseIfError(result) \
    if (result == ERROR) {   \
        return ERROR;        \
    }

// Return FILE_NOT_FOUND if the file pointer is NULL
#define raiseIfFileNotFound(filePointer) \
    if (filePointer == NULL) {           \
        return FILE_NOT_FOUND;           \
    }

// Db Request constants
#define DB_RESPONSE_SIZE 1024
#define DB_REQUEST_SIZE 1024

// Response templates
const char* successResponseJsonTemplate = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n%s";

// Send response to client
#define RESPOND(clientSocket, response) send(clientSocket, response, strlen(response), SEND_DEFAULT);

// static responses
// response must be a string literal
#define STATIC_RESPONSE(clientSocket, response) send(clientSocket, response, sizeof(response) - 1, SEND_DEFAULT);

const char badRequestResponse[] = "HTTP/1.1 400 Bad Request\nContent-Type: application/json\n\n{\"message\": \"Bad Request\"}";
#define BAD_REQUEST(clientSocket) STATIC_RESPONSE(clientSocket, badRequestResponse)

const char methodNotAllowedResponse[] = "HTTP/1.1 405 Method Not Allowed\nContent-Type: application/json\n\n{\"message\": \"Method not allowed\"}";
#define METHOD_NOT_ALLOWED(clientSocket) STATIC_RESPONSE(clientSocket, methodNotAllowedResponse)

const char notFoundResponse[] = "HTTP/1.1 404 Not Found\nContent-Type: application/json\n\n{\"message\": \"User Not Found\"}";
#define NOT_FOUND(clientSocket) STATIC_RESPONSE(clientSocket, notFoundResponse)

const char unprocessableEntityResponse[] = "HTTP/1.1 422 Unprocessable Entity\nContent-Type: application/json\n\n{\"message\": \"Unprocessable Entity\"}";
#define UNPROCESSABLE_ENTITY(clientSocket) STATIC_RESPONSE(clientSocket, unprocessableEntityResponse)

const char internalServerErrorResponse[] = "HTTP/1.1 500 Internal Server Error\nContent-Type: application/json\n\n{\"message\": \"Internal Server Error\"}";
#define INTERNAL_SERVER_ERROR(clientSocket) STATIC_RESPONSE(clientSocket, internalServerErrorResponse)

// HTTP methods
const char GET_METHOD[] = "GET";
const int GET_METHOD_LENGTH = sizeof(GET_METHOD) - 1;
const char POST_METHOD[] = "POST";
const int POST_METHOD_LENGTH = sizeof(POST_METHOD) - 1;

typedef struct sockaddr SA;
typedef struct sockaddr_in SA_IN;

// User struct constants
#define MAX_TRANSACTIONS 10
#define DATE_SIZE 32
#define DESCRIPTION_SIZE 32

typedef struct TRANSACTION {
    int valor;
    char tipo;
    char descricao[DESCRIPTION_SIZE];
    char realizada_em[DATE_SIZE];
} Transaction;

typedef struct USER {
    int id;
    int limit, total;
    int nTransactions;
    int oldestTransaction;
    Transaction transactions[MAX_TRANSACTIONS];
} User;

// Check socket errors
// Crash the program if the expression evaluates to ERROR
int check(int expression, const char* message);

// Compare two strings up to maxLength
int partialEqual(const char* str1, const char* str2, int maxLength);

// Gets system time and stores it in timeStr
void getCurrentTimeStr(char* timeStr);

// Convert number to binary, writes it to a char[4] array
void toBin(int number, char* binaryRepresentation);

// Convert binary representation on char[4] to it's number
int fromBin(char* binaryRepresentation);

// Send request to server and store the response in the response string
int clientRequest(int clientSocket, const char* request, int requestSize, char* response, int responseSize);

// Serialize user to a string
// returns the size of the serialized user in bytes
int serializeUser(User* user, char* serializedUser);

// Deserialize user from a string
void deserializeUser(char* serializedUser, User* user);

int check(int expression, const char* message) {
    if (expression == ERROR) {
        perror(message);
        exit(EXIT_FAILURE);
    }
    return expression;
}

int partialEqual(const char* str1, const char* str2, int maxLength) {
    for (int i = 0; i < maxLength; i++) {
        if (str1[i] == '\0' || str2[i] == '\0') {
            return false;
        }
        if (str1[i] != str2[i]) {
            return false;
        }
    }
    return true;
}

void getCurrentTimeStr(char* timeStr) {
    time_t mytime = time(NULL);
    char* time_str = ctime(&mytime);
    time_str[strlen(time_str) - 1] = '\0';
    strcpy(timeStr, time_str);
}

void toBin(int value, char* bin) {
    for (int i = 0; i < 4; i++) {
        bin[i] = (char)((value >> (i * 8)) & 0xFF);
    }
}

int fromBin(char* bin) {
    int value = 0;
    for (int i = 0; i < 4; i++) {
        value |= ((unsigned char)bin[i] << (i * 8));
    }
    return value;
}

int clientRequest(int clientSocket, const char* request, int requestSize, char* response, int responseSize) {
    send(clientSocket, request, requestSize, 0);
    int bytesRead = recv(clientSocket, response, responseSize, 0);
    if (bytesRead > 0) {
        response[bytesRead] = '\0';
    }
    return bytesRead;
}

int serializeUser(User* user, char* serializedUser) {
    int offset = 0;
    toBin(user->id, serializedUser);
    offset += 4;
    toBin(user->limit, &serializedUser[offset]);
    offset += 4;
    toBin(user->total, &serializedUser[offset]);
    offset += 4;
    toBin(user->nTransactions, &serializedUser[offset]);
    offset += 4;
    toBin(user->oldestTransaction, &serializedUser[offset]);
    offset += 4;
    for (int i = 0; i < user->nTransactions; i++) {
        toBin(user->transactions[i].valor, &serializedUser[offset]);
        offset += 4;
        serializedUser[offset] = user->transactions[i].tipo;
        offset += 1;
        for (int j = 0; j < DESCRIPTION_SIZE; j++) {
            serializedUser[offset] = user->transactions[i].descricao[j];
            offset += 1;
        }
        for (int j = 0; j < DATE_SIZE; j++) {
            serializedUser[offset] = user->transactions[i].realizada_em[j];
            offset += 1;
        }
    }
    return offset;
}

void deserializeUser(char* serializedUser, User* user) {
    char* idBin = &serializedUser[0];
    char* limitBin = &serializedUser[4];
    char* totalBin = &serializedUser[8];
    char* nTransactionsBin = &serializedUser[12];
    char* oldestTransactionBin = &serializedUser[16];
    int offset = 20;

    user->id = fromBin(idBin);
    user->limit = fromBin(limitBin);
    user->total = fromBin(totalBin);
    user->nTransactions = fromBin(nTransactionsBin);
    user->oldestTransaction = fromBin(oldestTransactionBin);

    for (int i = 0; i < user->nTransactions; i++) {
        char* valorBin = &serializedUser[offset];
        user->transactions[i].valor = fromBin(valorBin);
        offset += 4;

        char tipo = serializedUser[offset];
        user->transactions[i].tipo = tipo;
        offset += 1;

        for (int j = 0; j < DESCRIPTION_SIZE; j++) {
            user->transactions[i].descricao[j] = serializedUser[offset];
            offset += 1;
        }

        for (int j = 0; j < DATE_SIZE; j++) {
            user->transactions[i].realizada_em[j] = serializedUser[offset];
            offset += 1;
        }
    }
}

#endif