#ifndef DBCLIENT_H
#define DBCLIENT_H

// Header file for the database files
// Saves and reads user data to and from binary files

#include "helpers.h"

// You should only use if this if it's a new user, or you want to reset the user
// Instead of doing subsequent readUser and writeUser, use the updateUser function to update the user
// Returns ERROR if it fails to write the user to the database
int writeUser(int clientSocket, User* user);

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

#endif
