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


typedef struct transaction transaction;

struct transaction {
 int METHOD;
 int TRANSACTION_ID;
 int SEQUENCE_NUMBER;
 int CONTENT_LEN;
 char *data;
 };

char *processTransaction(transaction *txn);
void initializeFileSystem(const char* fullPath);

/// Gets the data in char* format. This data is from malloc, so must free after use
char *readFile(char *fileName);

void writeToFile(char *fileName, char *data, size_t len);

#endif
