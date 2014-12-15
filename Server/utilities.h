//
//  utilities.h
//  Server
//
//  Created by Oleg Zdornyy on 2014-11-25.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#ifndef __Server__utilities__
#define __Server__utilities__

#include <stdio.h>
#include "filesystem.h"
#include <queue>
/*
 * Checks if valid transaction occurred
 * Returns zero if NEW_TXN
 * Returns -1 if invalid
 * Returns transaction ID elsewise
 */
Transaction *parseRequest(const char *request);


/*
 * Processes request header and puts into Transaction header
 * Returns a malloc'ed Transaction header - must free after use
 */
bool checkValidTransactionID(char *message);

/* Sorts the messages into a queue with NEW_TXN at the top and COMMIT last
 *
 */
std::queue<Transaction *> sortMessages(std::queue<Transaction *> transactions);

#endif /* defined(__Server__utilities__) */
