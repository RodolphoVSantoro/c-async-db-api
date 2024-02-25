#ifndef DBCLIENT_H
#define DBCLIENT_H

// Header file for the database files
// Saves and reads user data to and from binary files

#include "helpers.h"

// Returns ERROR if the database fails to respond
int readUser(int clientSocket, User* user, int id);

// You should only use if this if it's a new user, or you want to reset the user
// Instead of doing subsequent readUser and writeUser, use the updateUser function to update the user
// Returns ERROR if it fails to write the user to the database
int writeUser(int clientSocket, User* user);

// updates the user with the transaction
// writes the updated user to the user variable
// returns SUCCESS if transaction was successful
// returns ERROR if it fails to lock the file
// returns FILE_NOT_FOUND if the user is not found
// returns LIMIT_EXCEEDED_ERROR if the user has no limit
// returns INVALID_TIPO_ERROR if the tipo is not valid
int updateUserWithTransaction(int clientSocket, int id, Transaction* transaction, User* user);

// Returns INVALID_TIPO_ERROR if the tipo is not valid
// Returns LIMIT_EXCEEDED_ERROR if the user has no limit
// Returns SUCCESS if the transaction was successful
int addTransaction(User* user, Transaction* transaction);

// Fills the orderedTransactions array with the user's transactions ordered by the oldest
void getOrderedTransactions(User* user, Transaction* orderedTransactions);

int connectToDb();

int connectToDb(int port) {
    int dbSocket = socket(AF_INET, SOCK_STREAM, 0);
    raiseIfError(dbSocket);

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(dbSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        return ERROR;
    }

    return dbSocket;
}

int writeUser(int clientSocket, User* user) {
    char response[DB_RESPONSE_SIZE];
    char request[DB_REQUEST_SIZE];
    request[0] = 'c';
    request[1] = ' ';
    toBin(user->id, &request[2]);
    toBin(user->limit, &request[6]);
    request[7] = '\0';
    int requestSize = 8;
    // 'c' id(binNum) limit(binNum)
    int responseSize = clientRequest(clientSocket, request, requestSize, response, DB_RESPONSE_SIZE);
    if (responseSize == -1) {
        return ERROR;
    }
    if (response[0] != '0') {
        return ERROR;
    }
    return SUCCESS;
}

int readUser(int clientSocket, User* user, int id) {
    char response[DB_RESPONSE_SIZE];
    char request[DB_REQUEST_SIZE];
    request[0] = 'r';
    request[1] = ' ';
    if (id > 9) {
        return ERROR;
    }
    request[2] = id + '0';
    request[3] = '\0';
    // 'r' id(binNum)
    int responseSize = clientRequest(clientSocket, request, 4, response, DB_RESPONSE_SIZE);
    if (responseSize == -1) {
        return ERROR;
    }
    if (response[0] != '0') {
        return ERROR;
    }

    deserializeUser(&response[2], user);

    return SUCCESS;
}

int updateUserWithTransaction(int clientSocket, int id, Transaction* transaction, User* user) {
    char response[DB_RESPONSE_SIZE];
    char request[DB_REQUEST_SIZE];
    request[0] = 'u';
    request[1] = ' ';

    char charId = id + '0';
    request[2] = charId;
    request[3] = ' ';
    request[4] = transaction->tipo;
    request[5] = ' ';
    toBin(transaction->valor, &request[6]);
    request[10] = ' ';
    strcpy(&request[11], transaction->descricao);

    int requestSize = 11 + DESCRIPTION_SIZE;

    // 'u' id(binNum) tipo('c' ou 'd') valor(binNum) descricao(char[DESCRIPTION_SIZE])
    int responseSize = clientRequest(clientSocket, request, requestSize, response, DB_RESPONSE_SIZE);
    if (responseSize == -1) {
        return ERROR;
    }
    if (response[0] != '0') {
        int result = response[0] - '0';
        return -result;
    }

    deserializeUser(&response[2], user);

    return SUCCESS;
}

void getOrderedTransactions(User* user, Transaction* orderedTransactions) {
    if (user->nTransactions == 0) {
        return;
    }
    int i = user->oldestTransaction;
    i = (i - 1 + user->nTransactions) % user->nTransactions;
    for (int j = 0; j < user->nTransactions; j++) {
        orderedTransactions[j] = user->transactions[i];
        i = (i - 1 + user->nTransactions) % user->nTransactions;
    }
}

#endif