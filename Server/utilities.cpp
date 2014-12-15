//
//  utilities.cpp
//  Server
//
//  Created by Oleg Zdornyy on 2014-11-25.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include <queue>
#include "utilities.h"
#include <string>
#include <string.h>
#include <cstdlib>

/*
 * Checks if valid transaction occurred
 * Returns zero if NEW_TXN
 * Returns -1 if invalid
 * Returns transaction ID elsewise
 */
bool checkValidTransactionID(char *message) {
    Transaction         txn;
    std::string         requestnew = message;
    std::string         delimeter = " ";
    std::string         delimeterNoData = "\r\n\r\n\r\n";
    std::string         delimeterData = "\r\n\r\n";
    std::string         tokens[4];
    int                 maxID;
    
    // Get the first 3 space delimited arguments
    for(int i =0; i <= 2; i++) {
        tokens[i] = requestnew.substr(0, requestnew.find(delimeter));
        requestnew.erase(0, tokens[i].length()+1);
    }
    
    txn.TRANSACTION_ID = atoi(&tokens[1][0]);
    maxID = getBiggestTransactionID();
    
    if(strcmp(&tokens[0][0], "NEW_TXN") == 0) {
        return 0;
    }
    
    if(maxID < txn.TRANSACTION_ID) {
        return -1;
    } else {
        return txn.TRANSACTION_ID;
    }
}


/* 
 * Processes request header and puts into Transaction header
 * Returns a malloc'ed Transaction header - must free after use
 */
Transaction *parseRequest(const char *request) {
    Transaction         *txn = (Transaction*) calloc(1, sizeof(Transaction));
    std::string         requestnew = request;
    std::string         delimeter = " ";
    std::string         delimeterNoData = "\r\n\r\n\r\n";
    std::string         delimeterData = "\r\n\r\n";
    std::string         tokens[5]; // 5 strings for all variables (including data)
    size_t              index;
    
    // Got the first 3 space delimited arguments
    for(int i =0; i <= 2; i++) {
        tokens[i] = requestnew.substr(0, requestnew.find(delimeter));
        requestnew.erase(0, tokens[i].length()+1);
    }
    
    txn->SEQUENCE_NUMBER = atoi(&tokens[2][0]);
    txn->TRANSACTION_ID = atoi(&tokens[1][0]);
    txn->SOCKET_FD = -1;
    
    // If no data delimiter found, check for data!
    index = requestnew.find(delimeterData);
    if(index != std::string::npos) {
        tokens[3] = requestnew.substr(0, index);
        requestnew.erase(0, tokens[3].length()+strlen("\r\n\r\n"));
        
        // Get length of datafile to allocate enough memory
        txn->CONTENT_LEN =atoi(&tokens[3][0]);
        tokens[4] = requestnew;
        
        // malloc and copy over the string
        txn->data = (char *)calloc(1, (size_t)txn->CONTENT_LEN+1);
        
        strcpy(txn->data, &tokens[4][0]);
    } else {
        txn->data = NULL;
        index = requestnew.find(delimeterNoData);
        tokens[3] = requestnew.substr(0, index);
    }
    if(strcmp(&tokens[0][0], "READ") == 0) {
        txn->METHOD = READ;
    } else if(strcmp(&tokens[0][0], "NEW_TXN") == 0) {
        txn->METHOD = NEW_TXN;
    } else if(strcmp(&tokens[0][0], "WRITE")== 0) {
        txn->METHOD = WRITE;
    } else if(strcmp(&tokens[0][0], "COMMIT")== 0) {
        txn->METHOD = COMMIT;
    } else if(strcmp(&tokens[0][0], "ABORT")== 0) {
        txn->METHOD = ABORT;
    }
    
    size_t totalSize = strlen(request)+2;
    txn->raw = (char*)calloc(1, totalSize);
    strcpy(txn->raw, request);
    strcat(txn->raw, "\n");
    
    return txn;
}

/* Sorts the messages into a queue with NEW_TXN at the top and COMMIT last
 *
 */
std::queue<Transaction *> sortMessages(std::queue<Transaction *> transactions) {
    size_t count = transactions.size();
    std::queue<Transaction *> returnQ;
    Transaction *temp[count];
    
    if(count == 0) {
        return transactions;
    }
    
    // Put all messages into array
    for(int i=(int)count-1; i >= 0; i--) {
        temp[i] = transactions.front();
        transactions.pop();
    }
    
    // Put the NEW_TXN into position 0, COMMIT in last position
    for(int i=0; i<count; i++) {
        if( (temp[i]->METHOD == NEW_TXN)  || (temp[i]->METHOD == COMMIT) ) {
            if(temp[i]->METHOD == NEW_TXN) {
                Transaction *x = temp[0];
                temp[0] = temp[i];
                temp[i] = x;
            } else if(temp[i]->METHOD == COMMIT) {
                Transaction *x = temp[count -1];
                temp[count -1 ] = temp[i];
                temp[i] = x;
            }
        }
    }
    
    // Sort the rest of the array (quadratic time)
    for(int i=1; i<count-2; i++) {
        for(int j=i+1; j<=count-2; j++) {
            if(temp[i]->SEQUENCE_NUMBER > temp[j]->SEQUENCE_NUMBER) {
                Transaction *swap = temp[i];
                temp[i] = temp[j];
                temp[j] = swap;
            }
        }
    }
    
    // Put back into new queue
    for(int i=0; i<count; i++) {
        returnQ.push(temp[i]);
    }
    
    return returnQ;
}