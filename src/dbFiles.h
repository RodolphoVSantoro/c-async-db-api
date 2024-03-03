#ifndef DBFILES_H
#define DBFILES_H

// Header file for the database files
// Saves and reads user data to and from binary files

#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include "helpers.h"

// Comment this line to keep the database on server start
#define RESET_DB 1

// Open file modes
#define WRITE_BINARY "wb"
#define READ_WRITE_BINARY "rb+"

// User File name template
const char* userFileTemplate = "data/user%d.bin";

// Initial database setup
const int userInitialLimits[] = {100000, 80000, 1000000, 10000000, 500000};
const int numberInitialUsers = sizeof(userInitialLimits) / sizeof(int);

// move right on a circular array
#define moveRightInTransactions(index) (index = (index + 1) % MAX_TRANSACTIONS)

// user file name size
#define FILE_NAME_SIZE 32

// Initializes the database with 5 users
// Returns ERROR it fails to write a user to the file
// Returns SUCCESS if the database was successfully initialized
int initDb();

// Returns ERROR if the file is not found
int readUser(User* user, int id);

// You should only use if this if it's a new user, or you want to reset the user
// Instead of doing subsequent readUser and writeUser, use the updateUser function to update the user
// Returns ERROR if it fails to write the user to the file
int writeUser(User* user);

// updates the user with the transaction
// id is the user id
// transaction is the transaction to be added to the user
// user returns the updated user
// writes the updated user to the user variable
// returns SUCCESS if transaction was successful
// returns ERROR if it fails to lock the file
// returns FILE_NOT_FOUND if the user is not found
// returns LIMIT_EXCEEDED_ERROR if the user has no limit
// returns INVALID_TIPO_ERROR if the tipo is not valid
int updateUserWithTransaction(int id, Transaction* transaction, User* user);

// Returns INVALID_TIPO_ERROR if the tipo is not valid
// Returns LIMIT_EXCEEDED_ERROR if the user has no limit
// Returns SUCCESS if the transaction was successful
int addTransaction(User* user, Transaction* transaction);
// Tries to add or subtract the transaction value from the user's total
// Returns ERROR if the user doesn't have enough limit
int addSaldo(User* user, Transaction* transaction);

// Closes all the open files
void closeDBFiles();

#define MAX_USERS 10
FILE* userFiles[MAX_USERS] = {NULL};
int userFileNo[MAX_USERS] = {0};

int initDb() {
    User user;
    user.total = 0;
    user.nTransactions = 0;
    user.oldestTransaction = 0;

    for (int id = 0; id < numberInitialUsers; id++) {
        user.id = id + 1;
        user.limit = userInitialLimits[id];
        int writeResult = writeUser(&user);
        if (writeResult == ERROR) {
            return ERROR;
        }
    }

    return SUCCESS;
}

void closeDBFiles() {
    for (int i = 0; i < 10; i++) {
        if (userFiles[i] != NULL) {
            fclose(userFiles[i]);
        }
    }
}

int getUserFile(int id, int* fileNo, FILE** file) {
    if (id < 0 || id > MAX_USERS) {
        return FILE_NOT_FOUND;
    }
    if (userFiles[id] == NULL) {
        char fname[FILE_NAME_SIZE];
        sprintf(fname, userFileTemplate, id);

        if ((access(fname, F_OK) != 0)) {
            return FILE_NOT_FOUND;
        }

        userFiles[id] = fopen(fname, READ_WRITE_BINARY);
        errIfNull(userFiles[id]);
        userFileNo[id] = fileno(userFiles[id]);
    } else {
        int seekResult = fseek(userFiles[id], 0, SEEK_SET);
        raiseIfError(seekResult);
    }
    *file = userFiles[id];
    *fileNo = userFileNo[id];
    return SUCCESS;
}

int writeUser(User* user) {
    char fname[FILE_NAME_SIZE];
    sprintf(fname, userFileTemplate, user->id);

    if ((access(fname, F_OK) != 0)) {
        FILE* createFile = fopen(fname, WRITE_BINARY);
        errIfNull(createFile);
        fclose(createFile);
    }

    FILE* fpTotals;
    int fpTotalsFileDescriptor;
    int getFileResult = getUserFile(user->id, &fpTotalsFileDescriptor, &fpTotals);
    if (getFileResult != SUCCESS) {
        return getFileResult;
    }
    int writeResult = fwrite(user, sizeof(User), 1, fpTotals);
    raiseIfError(writeResult);
    int flushResult = fflush(fpTotals);
    raiseIfError(flushResult);
    return SUCCESS;
}

int readUser(User* user, int id) {
    FILE* fpTotals;
    int fpTotalsFileDescriptor;
    int getFileResult = getUserFile(id, &fpTotalsFileDescriptor, &fpTotals);
    if (getFileResult != SUCCESS) {
        return getFileResult;
    }

    int readResult = fread(user, sizeof(User), 1, fpTotals);
    raiseIfError(readResult);

    return SUCCESS;
}

int updateUserWithTransaction(int id, Transaction* transaction, User* user) {
    char fname[FILE_NAME_SIZE];
    sprintf(fname, userFileTemplate, id);

    if ((access(fname, F_OK) != 0)) {
        return ERROR;
    }

    FILE* fpTotals;
    int fpTotalsFileDescriptor;
    int getFileResult = getUserFile(id, &fpTotalsFileDescriptor, &fpTotals);
    if (getFileResult != SUCCESS) {
        return getFileResult;
    }

    int readResult = fread(user, sizeof(User), 1, fpTotals);
    raiseIfError(readResult);

    int transactionResult = addTransaction(user, transaction);

    if (transactionResult == 0) {
        // Go back to the beginning of the file, because fread moved the cursor
        int seekResult = fseek(fpTotals, 0, SEEK_SET);
        raiseIfError(seekResult);
        int writeResult = fwrite(user, sizeof(User), 1, fpTotals);
        raiseIfError(writeResult);
    }
    int flushResult = fflush(fpTotals);
    raiseIfError(flushResult);
    return transactionResult;
}

int addTransaction(User* user, Transaction* transaction) {
    int resultSaldo = addSaldo(user, transaction);
    if (resultSaldo != SUCCESS) {
        return resultSaldo;
    }
    if (user->nTransactions == 10) {
        user->transactions[user->oldestTransaction] = *transaction;
        moveRightInTransactions(user->oldestTransaction);
        return SUCCESS;
    }

    user->transactions[user->nTransactions] = *transaction;
    user->nTransactions++;
    return SUCCESS;
}

int addSaldo(User* user, Transaction* transaction) {
    if (transaction->tipo == 'd') {
        int newTotal = user->total - transaction->valor;
        if (-1 * newTotal > user->limit) {
            return LIMIT_EXCEEDED_ERROR;
        }
        user->total = newTotal;
        return SUCCESS;
    } else if (transaction->tipo == 'c') {
        user->total += transaction->valor;
        return SUCCESS;
    }
    return INVALID_TIPO_ERROR;
}

#endif