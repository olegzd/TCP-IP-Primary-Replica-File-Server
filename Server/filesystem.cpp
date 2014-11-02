//
//  Transaction.cpp
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-27.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include "filesystem.h"

#include <algorithm> // sort
#include <stdio.h>
#include <vector>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <string> // for std::string
#include <queue>
#include <mutex>
#include <sys/socket.h>
#include <map>
#include "server.h"

#define LOG_FILE_NAME ".log"
#define INCOMING_BUFFER_LEN 4096
#define OUTPUT_BUFFER_LEN 1024

/*
 * Variables used globally by filesystem.cpp
 *
 */
std::map<std::string, pthread_mutex_t>    fileHash;
std::map<std::string, int>          test;
std::map<int, bool>                 transactionComplete;
char                                directory[256];
char                                *readFile(char *fileName);
pthread_mutex_t                     id_mutex;
pthread_mutex_t                     log_mutex;
int                                 lastTransactionId=0;
FILE                                *logFile;

/*
 * Global variables.
 *
 */
// Global condition variables.
std::map<int, pthread_cond_t>    conditionVars; // Used to signal a thread with TRANSACTION_ID (int)
pthread_mutex_t                  conditionsMutex; // Mutex that must be locked to access reconnectedSocket
std::map<int, Transaction*>      reconnectedSocket; // Holds the latest Transaction struct it got

/*
 * Method pre-declarations
 *
 */
void startFakeNewTranscation(Transaction [], int dataLen);

void recoveryCheck() {
    //
    //
    // Check if the log file exists.
    // If not, this is the first time this server has been run in the directory.
    // Create a new one.
    //
    //
    char filePath[sizeof(directory) + sizeof("./log") +1]; // +1 for EOF
    sprintf(filePath, "%s%s", directory, "/.log");
    
    std::string temp(".log");
    if(fileHash.count(temp) >0 ) {
        //
        // Insert recovery functionality here
        //
        logFile = fopen(filePath, "a+");
    } else { // Create new .log file in this directory
        pthread_mutex_init(&fileHash[LOG_FILE_NAME], NULL);
        logFile = fopen(filePath, "w+");
    }
}

/*
 *
 *
 */
void initializeFileSystem(const char* fullPath, char *ip, char * port) {
    DIR         *dir;
    dirent      *file;
    
    
    dir = opendir(fullPath);

    pthread_mutex_init(&id_mutex, NULL);
    pthread_mutex_init(&log_mutex, NULL);
    pthread_mutex_init(&conditionsMutex, NULL);
    
    //
    //
    // Check what files exist and add them to the hash table.
    // Initialize mutexes for each file.
    // This dir check catches hidden files (including log)
    //
    //
    while( (file = readdir(dir)) != NULL) {
        // Don't bother with . or .. files
        if( strncmp(file->d_name, "..", sizeof("..")) && strncmp(file->d_name, ".", sizeof(".")) ) {
            std::string temp(file->d_name);
            pthread_mutex_init(&fileHash[temp], NULL);
        }
    }
    closedir(dir);
    
    strncpy(directory, fullPath, sizeof(directory));
    
    printf("File system initialized to %s\n", directory);
    
    recoveryCheck();
    
}

/*
 *
 *
 */
int getNewTransactionID() {
    int id = -1;
    
    pthread_mutex_lock(&id_mutex);
    id = lastTransactionId +=1;
    lastTransactionId++;
    pthread_mutex_unlock(&id_mutex);
    
    return id;
}

/* Get latest transaction ID
 *
 *
 */
int getBiggestTransactionID() {
    int id = -1;
    
    pthread_mutex_lock(&id_mutex);
    id = lastTransactionId;
    pthread_mutex_unlock(&id_mutex);
    
    return id;
}

/*
 * Check a queue of transactions and see if any are missing.
 *
 * If any are missing, return a queue of the missing sequences.
 */
std::queue<int> checkMissingSequences( std::queue<Transaction *> transactions ) {
    int             lastSequence     = transactions.back()->SEQUENCE_NUMBER; // Last transaction
    size_t          count               = transactions.size();
    int             sequences[count];
    std::queue<int> missingSequences;
    

    // memset sequences[] to zero for missing sequences to evaluate to zero
    memset(&sequences, 0, sizeof(sequences));

    // Get all sequences
    for(int i=0; i <= lastSequence; i++){
        Transaction *temp = transactions.front();

        sequences[temp->SEQUENCE_NUMBER] = 1;
        printf("Current sequence number: %i\n", temp->SEQUENCE_NUMBER);
        
        transactions.pop();
        transactions.push(temp);
    }
    
    // Check for missing sequence numbers
    for(int i=0; i <= lastSequence; i++){
        printf("Comparing i:%i and sequences[%i]:%i\n", i, i, sequences[i]);
        if(1 != sequences[i]) {
            missingSequences.push(i);
            printf("Missing a sequence!\n");
        }
    }
    return missingSequences;
}

/*
 *
 *
 */
void requestMissingMessages(std::queue<int> missingMessages, const char *ip, const char *port) {
    printf("Pinging back client for missing messages");
}


/* Sorts the messages into a queue with NEW_TXN at the top and COMMIT last
 *
 * If time permits, optimize this as it's a 3*n + n^2 beast of a sorting function
 */
std::queue<Transaction *> sortMessages(std::queue<Transaction *> transactions) {
    size_t count = transactions.size();
    std::queue<Transaction *> returnQ;
    Transaction *temp[count];
    
     // Put all messages into array
     for(int i=(int)count-1; i >= 0; i--) {
         temp[i] = transactions.front();
         transactions.pop();
     }
    
     // Put the NEW_TXN into position 0, COMMIT in last position
     for(int i=0; i<count; i++) {
         if(temp[i]->METHOD == NEW_TXN || COMMIT) {
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

/*
 *
 * Writes the transaction to log
 *
 */
void writeMessageToLog( Transaction *tx ) {
    pthread_mutex_lock(&log_mutex);
    fprintf(logFile, "%s", tx->raw);
    fflush(logFile);
    fflush(logFile);
    pthread_mutex_unlock(&log_mutex);
}

void writeToDisk(std::queue<Transaction *> messages) {
    FILE *file;
    std::string fname(messages.front()->data);
    size_t count = messages.size();
    
    // Get lock for file. First, check if it exists
    if(fileHash.count(fname) <= 0) {
        pthread_mutex_init(&fileHash[fname], NULL);
        pthread_mutex_lock(&fileHash[fname]);
    } else {
        pthread_mutex_lock(&fileHash[fname]);
    }
    
    // Open file (fileName)
    char filePath[strlen(directory)+strlen(fname.c_str()) + 2]; // Size of directory + file name + 1 (for '/')
    sprintf(filePath, "%s/%s", directory, fname.c_str());
    file = fopen(filePath, "a+");
    printf("File path is %s\n", filePath);
    
    // Get transaction
    Transaction *temp = messages.front();
    messages.pop();
    free(temp);
    
    // Write to file excluding the first and last parts of the array
    for(int i =1; i < count-1; i++) {
        Transaction *toFree = messages.front(); printf("Writing to file: %s\n", toFree->data);
        fprintf(file, "%s", toFree->data);
        messages.pop();
        free(toFree);
    }
    
    // Force flush to disk & close.
    fflush(file);
    fflush(file);
    fclose(file);
    
    // Free commit message
    temp = messages.front();
    if(temp->data != NULL) {
        free(temp->data);
    }
    free(temp->raw);
    free(temp);
    
    pthread_mutex_unlock(&fileHash[fname]);
    
}

/*
 *
 * Fake new Transaction dealing mechanisms - will eventually be a threaded mechanism
 *
 */
void *startNewTranscation(void *socketfd) {
    
    // Transaction     *newtx = tx[0]; //  TESTING - holds the NEW_TXN datagram
    std::queue<Transaction*> transactions; // HOLDS THE TRANSACTIONS (IN RAM & WRITES OUT)
    //int               currentSequence;
    int                 transactionId = -2000;
    char                *filename = NULL;
    // int              sumOfTransactions = 0;
    // int              lastTransaction = -1;
    std::queue<int> missing;
    int TRANSACTION_METHOD = -1;
    
    char data[INCOMING_BUFFER_LEN];
    char reply[OUTPUT_BUFFER_LEN];
    
    int socket = *(int*)socketfd;
    
    printf("Connection received\n");

    while(1) {

        
        // Get data.
        ssize_t incoming = 0;
        
        // Fragmentation loop - get byte by byte
        while( (incoming =recv(socket, data+incoming, INCOMING_BUFFER_LEN-incoming, 0) ) != 0 ) {
            if( (strstr(data, "\r\n\r\n\r\n") != NULL) || (strstr(data, "\r\n\r\n") != NULL)  ){
                // We got the expected end of string.
                printf("Got data! %s\n", data);
                break;
            }
        }
RECONNECTED:
        Transaction *tx;
        
        // Check that connection hasn't been closed. If so, goto MONITOR.
        if(incoming == 0) {
            if(transactionId == -2000) {
                return NULL;
            }
            printf("Connection closed. Going to MONITOR.\n");
            goto MONITOR;
        }
        
        data[incoming] = '\0';
        
        // Parse message.
        printf("Incoming data. Size: %lu.  Data: %s\n\n",incoming, data);
        tx = parseRequest(data);
        
        // Check if this message is meant for a different transaction
        if(transactionId == -2000) {
            if(tx->METHOD != NEW_TXN) {
                if(transactionComplete[tx->TRANSACTION_ID]) {
                    // Invalid Transaction ID - send back an error
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 201, 0, "\r\n\r\nInvalid TXN ID");
                    printf("Sending back: %s\n", reply);
                    send(socket, reply, strlen(reply), 0);
                } else {
                    printf("Closing connection - this is a valid ID\n");
                    // This is a valid transaction ID
                    // Signal condition of thread with this Transaction ID
                    // and put data into shared hash.
                    tx->SOCKET_FD = socket;
                    pthread_mutex_lock(&conditionsMutex);
                    
                    reconnectedSocket[tx->TRANSACTION_ID] = tx;
                    
                    // Signal existing thread that a new socket exists.
                    pthread_cond_signal(&conditionVars[tx->TRANSACTION_ID]);
                    
                    pthread_mutex_unlock(&conditionsMutex);
                    goto CLOSE_CONNECTION;
                }
            }
        }
        // If this is a reconnected connection, tx will
        // have a new socket descriptor.
        if(tx->SOCKET_FD != -1) {
            printf("Assigning new socket %i\n", tx->SOCKET_FD);
            socket = tx->SOCKET_FD;
        }
        
        // Check to make sure we have a valid TRANSACTION IDs.
        if(transactionId != -2000) {
            if(tx->TRANSACTION_ID != transactionId && tx->METHOD != NEW_TXN) {
                // Send ERROR back to server.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 201, 0, "\r\n\r\nInvalid TXN ID");
                printf("Sending back: %s\n", reply);
                send(socket, reply, strlen(reply), 0);
            }
        }
        switch (tx->METHOD) {
            case NEW_TXN:
                
                printf("Starting new transaction\n");
                transactionId = getNewTransactionID();
                filename = tx->data;
                
                // Write to log.
                writeMessageToLog(tx);
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                // Write to RAM.
                transactions.push(tx);
                
                // Initialize the condition variable
                pthread_mutex_lock(&conditionsMutex);
                pthread_cond_init(&conditionVars[transactionId], NULL);
                pthread_mutex_unlock(&conditionsMutex);
                
                continue;
            case READ:
                
                // Write to log.
                writeMessageToLog(tx);
                TRANSACTION_METHOD = READ;
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                // Write to RAM.
                transactions.push(tx);
                continue;
            case WRITE:
               
                // Write to log.
                writeMessageToLog(tx);
                TRANSACTION_METHOD = WRITE;
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                // Write to RAM.
                transactions.push(tx);
                continue;
            case COMMIT:
                // Write to log.
                writeMessageToLog(tx);
                
                // Check for missing sequences.
                missing = checkMissingSequences(transactions);
                if(missing.size() > 0) {
                    printf("We're missing sequences!\n");
                    continue;
                } else {
                    transactions = sortMessages(transactions);
                    
                    // Send ACK back.
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                    send(socket, reply, strlen(reply), 0);
                    
                    // Write to RAM.
                    transactions.push(tx);
                    
                    // Only reaches here if transaction successfully commited.
                    if (TRANSACTION_METHOD == WRITE) {
                        printf("Writing\n");
                        writeToDisk(transactions);
                    }
                    if (TRANSACTION_METHOD == READ) {
                        char *readin = readFile(filename);
                        printf("READING: %s\n", readin);
                        goto CLEANUP;
                    }
                }
                break;
            case ABORT:
                writeMessageToLog(tx);
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                printf("Transaction aborted\n");
                goto CLEANUP;
            default:
                printf("Invalid Transaction. Closing connection.\n");
                goto CLEANUP;
        } // end of switch.
        
MONITOR:
        printf("Waiting until connection reachieved\n");

        pthread_mutex_lock(&conditionsMutex);
        pthread_cond_wait(&conditionVars[transactionId], &conditionsMutex);
        
        tx = reconnectedSocket[transactionId];
        printf("Reconnected socket: %i\n", tx->SOCKET_FD);
        
        pthread_mutex_unlock(&conditionsMutex);
        printf("Connection RE Achieved!\n");
        goto RECONNECTED;
        
    } // end of while.



CLEANUP:
    transactionComplete[transactionId] = true;
    for(int i=0; i <transactions.size(); i++) {
        Transaction *temp = transactions.front();
        transactions.pop();
        
        if(temp->data != NULL) {
            free(temp->data);
        }
        free(temp->raw);
        free(temp);
    }
CLOSE_CONNECTION:
    printf("closing connection\n");
    decrease_connection_sem();
    return NULL;
}



/*
 * Read a file from directory/filename.
 *
 * Returns either null or a malloc'ed string
 */
char *readFile(char *fileName) {
    try {
        struct stat     st;
        char            path[350];
        FILE            *file;
        size_t          fileSize;
        
        // Lock the file mutex (block if needed)
        pthread_mutex_lock(&fileHash[fileName]);
        
        // Get the path
        sprintf(path, "%s/%s",directory,fileName);
        
        file = fopen(path, "r");
        if(file == NULL) {
            printf("Error opening file \"%s\" . %s\n", path, strerror(errno));
            return NULL;
        }

        // Get file information
        if( stat(path, &st) == 0 ) {
            fileSize = st.st_size;
        } else {
            printf("Unable to get info on file \"%s\" . %s\n", path, strerror(errno));
            return NULL;
        }
        
        
        // Malloc enough data, read it,
        // check for errors, and return the result
        char *data = (char*)malloc(fileSize+1);
        if(fileSize < fread(data, 1, fileSize, file) ) {
            printf("Error reading file %s\n", fileName);
            free(data);
            return NULL;
        }
        fclose(file);
        data[fileSize] = '\0'; // Always good to have EOF
        return data;
    } catch (std::exception e) {
        printf("Cannot open file %s. %s\n", fileName, e.what());
        return NULL;
    }
}

// writes len characters of data to fileName in directory
void writeToFile(char *fileName, char *data, size_t len) {
    char path[350];
    FILE *file;
    
    sprintf(path, "%s/%s",directory,fileName);
    
    // Get the lock on the file
    pthread_mutex_lock(&fileHash[fileName]);
    
    // Open file with append set as its mode
    file = fopen(path, "a");
    if(file == NULL) {
        printf("Error opening file \"%s\" . %s\n", path, strerror(errno));
        pthread_mutex_unlock(&fileHash[fileName]);
        return;
    }
    
    // All is good, time to write to file
    if( fwrite(data, 1, len, file) < len ) {
        printf("Error writing to %s\n", fileName);
        pthread_mutex_unlock(&fileHash[fileName]);
        return;
    }
    
    printf("Successfully written to file - insert ack return here\n");
    pthread_mutex_unlock(&fileHash[fileName]);
}

/*
 *
 * Fake new Transaction dealing mechanisms - will eventually be a threaded mechanism
 *
 */
void startFakeNewTranscation(Transaction *tx[], int count) {
    // Transaction     *newtx = tx[0]; //  TESTING - holds the NEW_TXN datagram
    std::queue<Transaction*> transactions; // HOLDS THE TRANSACTIONS (IN RAM & WRITES OUT)
    //int             currentSequence;
    int             transactionId;
    char            *filename = NULL;
    // int             sumOfTransactions = 0;
    // int             lastTransaction = -1;
    std::queue<int> missing;
    int TRANSACTION_METHOD = -1;
    
    // Replace with a listening mechanism
    for(int i = 0; i < count; i++) {
        switch (tx[i]->METHOD) {
            case NEW_TXN:
                
                printf("Starting new transaction\n");
                transactionId = getNewTransactionID();
                filename = tx[i]->data;
                
                if(filename == NULL) {
                    printf("No filename!\n");
                    return;
                }
                
                transactions.push(tx[i]);
                writeMessageToLog(tx[i]);
                break;
            case READ:
                writeMessageToLog(tx[i]);
                TRANSACTION_METHOD = READ;
                break;
            case WRITE:
                transactions.push(tx[i]);
                writeMessageToLog(tx[i]);
                TRANSACTION_METHOD = WRITE;
                break;
            case COMMIT:
                transactions.push(tx[i]);
                missing = checkMissingSequences(transactions);
                if(missing.size() > 0) {
                    // run algorithm get missing sequences
                    printf("We're missing sequences!\n");
                    return;
                } else {
                    transactions = sortMessages(transactions);
                    writeMessageToLog(tx[i]);
                }
                break;
            case ABORT:
                writeMessageToLog(tx[i]);
                printf("Transaction aborted\n");
                goto CLEANUP;
            default:
                printf("Invalid Transaction\n");
                return;
        }
        
    }
    
    if (TRANSACTION_METHOD == WRITE) {
        writeToDisk(transactions);
        return;
    }
    if (TRANSACTION_METHOD == READ) {
        char *readin = readFile(filename);
        printf("READING: %s\n", readin);
        goto CLEANUP;
    }
CLEANUP:
    for(int i=0; i <transactions.size(); i++) {
        Transaction *temp = transactions.front();
        transactions.pop();
        
        if(temp->data != NULL) {
            free(temp->data);
        }
        free(temp->raw);
        free(temp);
    }
}











