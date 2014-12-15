//
//  filesystem.h
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-27.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#ifndef Server_filesystem_h
#define Server_filesystem_h

#include <sys/types.h>
#include "operations.h"

#define REPLICA_LOG_FILE_NAME ".replicate_backlog"

typedef struct Transaction Transaction;

struct Transaction {
    int METHOD;
    int TRANSACTION_ID;
    int SEQUENCE_NUMBER;
    int CONTENT_LEN;
    int SOCKET_FD = -1;
    char *data;
    char *raw;
    
    // Comparison operator
    bool operator <(const Transaction  &other) {return SEQUENCE_NUMBER < other.SEQUENCE_NUMBER; };
 };


char *processTransaction(Transaction *txn);
void initializeFileSystem(const char* fullPath, char *ip, char *port);
int getBiggestTransactionID();
int tryReachReplica();

void *recoveryCheck( void *args );
void setReplicaSocket(int socketfd);
/// Gets the data in char* format. This data is from malloc, so must free after use
char *readFile(char *fileName);

void startFakeNewTranscation(Transaction *tx[], int count);

void writeToFile(char *fileName, char *data, size_t len);

void *PRIMARY_startNewTranscation(void *socketfd);

void *BACKUP_service(void *socketfd);

char *getFileSystemPath();


#endif
