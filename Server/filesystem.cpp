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
#include <stack>
#include <pthread.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <string> // for std::string
#include <queue>
#include <mutex>
#include <sys/socket.h>
#include <map>
#include "recoveryClient.h"
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
std::map<int, bool>                 transactionAllowed;
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


void *recoveryCheck( void *args ) {
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
    int messageCount = 0;
    if(fileHash.count(temp) >0 ) {
        //
        // Insert recovery functionality here
        //
        logFile = fopen(filePath, "a+");
        fseek(logFile, 0, SEEK_END);        // Get end of file
        size_t filesize = ftell(logFile);   // Get size of file
        rewind(logFile);                    // Rewind to beginning
        
        char *filebuffer = (char*) calloc(1, filesize);
        size_t readin = fread(filebuffer, 1, filesize, logFile);
        
        // Error check and recover
        if(readin != filesize) {
            printf("Error reading recovery log! This file system will now act as if clean state.");
            fclose(logFile);
            logFile = fopen(filePath, "w+");
            return NULL;
        }
        
        // Look for end of EOC
        std::string         delimeter = "EOC";
        std::string         filestring(filebuffer);
        size_t              index;
        std::queue<std::string> messages;
        std::queue<Transaction *> convertedTransactions;
        std::map<int, std::queue<Transaction *> > transactionHash;
        typedef std::map<int, std::queue<Transaction *> >::iterator transactionIterator;
        
        int secondaryIndex = 0;
        while( (index = filestring.find(delimeter)) != std::string::npos) {
            std::string token = filestring.substr(0, index);
            filestring.erase(0, token.length() + strlen("EOC"));
            messages.push( token );
            secondaryIndex += index + 3;
        }
        
        // Some failed transaction occured here, remove it.
        if(secondaryIndex < filesize-1) {
            ftruncate(fileno(logFile), secondaryIndex);
        }
        
        
        // Convert strings into Tranasction structs.
        // Add structs to hash, indexed by transaction id.
        // If any of structs is commit or abort, remove it.
        
        messageCount = messages.size();
        int maxTransactionID = 0;
        
        for(int message = 0; message < messageCount; message++) {
            
            Transaction *reference = parseRequest( messages.front().c_str() );
            messages.pop();
            
            if( reference->TRANSACTION_ID > maxTransactionID) maxTransactionID = reference->TRANSACTION_ID;
            
            transactionHash[reference->TRANSACTION_ID].push(reference); // Push into hash (so we have at least one variable)
            
            // If message is aborted/commited, remove hash.
            if( (reference->METHOD == ABORT) || (reference->METHOD == COMMIT ) ) {
                transactionHash.erase(reference->TRANSACTION_ID);
            }
            
        }
        
        // Connect, send, and disconnect.
        transactionIterator it =  transactionHash.begin();
        
        free(filebuffer);
        
        // Don't worry about critical section - it won't be competing for anything before we finish this call.
        lastTransactionId = maxTransactionID;
        
        pthread_t recoveryThread;
        pthread_create(&recoveryThread, NULL, recoveryClient, (void*)&transactionHash);
        pthread_join(recoveryThread, NULL);
        
    } else { // Create new .log file in this directory
        pthread_mutex_init(&fileHash[LOG_FILE_NAME], NULL);
        logFile = fopen(filePath, "w+");
    }
    
    return NULL;
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
std::queue<int> checkMissingSequences( std::queue<Transaction *> transactions, int lastSequence ) {
    size_t          count               = transactions.size();
    int             sequences[count];
    std::queue<int> missingSequences;
    
    printf("Last sequence %i\n", lastSequence);
    printf("Transaction size %lu\n", transactions.size());
    
    // memset sequences[] to zero for missing sequences to evaluate to zero
    memset(&sequences, 0, sizeof(sequences));
    
    // Get all sequences
    for(int i=0; i <= lastSequence; i++){
        Transaction *temp = transactions.front();
        
        sequences[temp->SEQUENCE_NUMBER] = 1;
        transactions.pop();
        transactions.push(temp);
    }
    
    // Check for missing sequence numbers
    for(int i=0; i <= lastSequence; i++){
        if(1 != sequences[i]) {
            missingSequences.push(i);
            printf("Missing a sequence: %i!\n", i);
        }
    }
    return missingSequences;
}

/*
 *
 *
 */
void requestMissingMessages(std::queue<int> missingMessages, const char *ip, const char *port) {
    
}


/* Sorts the messages into a queue with NEW_TXN at the top and COMMIT last
 *
 * If time permits, optimize this as it's a 3*n + n^2 beast of a sorting function
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

/*
 *
 * Writes the transaction to log
 *
 */
void writeMessageToLog( Transaction *tx ) {
    // Sometimes, the raw data doesn't have the new
    // Transaction IDs
    
    std::string raw (tx->raw);
    std::string del(" ");
    
    int startOfID = raw.find(del); // Find method
    int endOfID = raw.find(del, startOfID+1); // Skip first space
    
    // Get the ID
    int tid = atoi( raw.substr(startOfID, endOfID).c_str() );
    
    char newId[256];
    sprintf(newId, " %i", tx->TRANSACTION_ID);
    
    if( tid != tx->TRANSACTION_ID ) {
        raw.replace(startOfID, raw.substr(startOfID, endOfID-startOfID).length(), newId );
    }
    
    
    
    pthread_mutex_lock(&log_mutex);
    
    fprintf(logFile, "%sEOC", raw.c_str());
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
    printf("Number of writes %i\n", count-2);
    
    // Get transaction
    Transaction *temp = messages.front();
    messages.pop();
    free(temp);
    
    // Write to file excluding the first and last parts of the array
    for(int i =1; i < count-2; i++) {
        Transaction *toFree = messages.front(); printf("Writing part %i of %i to file: %s\n", i, (count-2), toFree->data);
        fprintf(file, "%s", toFree->data);
        messages.pop();
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
    
    pthread_mutex_unlock(&fileHash[fname]);
    
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
        char *data = (char*)calloc(1, fileSize+1);
        if(fileSize < fread(data, 1, fileSize, file) ) {
            printf("Error reading file %s\n", fileName);
            free(data);
            return NULL;
        }
        fclose(file);
        pthread_mutex_unlock(&fileHash[fileName]);
        
        data[fileSize] = '\0'; // Always good to have EOF
        return data;
    } catch (std::exception e) {
        printf("Cannot open file %s. %s\n", fileName, e.what());
        pthread_mutex_unlock(&fileHash[fileName]);
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
//void startFakeNewTranscation(Transaction *tx[], int count) {
//    std::queue<Transaction*> transactions; // HOLDS THE TRANSACTIONS (IN RAM & WRITES OUT)
//    int             transactionId;
//    char            *filename = NULL;
//    std::queue<int> missing;
//    int TRANSACTION_METHOD = -1;
//
//    // Replace with a listening mechanism
//    for(int i = 0; i < count; i++) {
//        switch (tx[i]->METHOD) {
//            case NEW_TXN:
//
//                printf("Starting new transaction\n");
//                transactionId = getNewTransactionID();
//                filename = tx[i]->data;
//
//                if(filename == NULL) {
//                    printf("No filename!\n");
//                    return;
//                }
//
//                transactions.push(tx[i]);
//                writeMessageToLog(tx[i]);
//                break;
//            case READ:
//                writeMessageToLog(tx[i]);
//                TRANSACTION_METHOD = READ;
//                break;
//            case WRITE:
//                transactions.push(tx[i]);
//                writeMessageToLog(tx[i]);
//                TRANSACTION_METHOD = WRITE;
//                break;
//            case COMMIT:
//                transactions.push(tx[i]);
//                missing = checkMissingSequences(transactions);
//                if(missing.size() > 0) {
//                    // run algorithm get missing sequences
//                    printf("We're missing sequences!\n");
//                    return;
//                } else {
//                    transactions = sortMessages(transactions);
//                    writeMessageToLog(tx[i]);
//                }
//                break;
//            case ABORT:
//                writeMessageToLog(tx[i]);
//                printf("Transaction aborted\n");
//                goto CLEANUP;
//            default:
//                printf("Invalid Transaction\n");
//                return;
//        }
//
//    }
//
//    if (TRANSACTION_METHOD == WRITE) {
//        writeToDisk(transactions);
//        return;
//    }
//    if (TRANSACTION_METHOD == READ) {
//        char *readin = readFile(filename);
//        printf("READING: %s\n", readin);
//        goto CLEANUP;
//    }
//CLEANUP:
//    for(int i=0; i <transactions.size(); i++) {
//        Transaction *temp = transactions.front();
//        transactions.pop();
//
//        if(temp->data != NULL) {
//            free(temp->data);
//        }
//        free(temp->raw);
//        free(temp);
//    }
//}




/*
 *
 * Fake new Transaction dealing mechanisms - will eventually be a threaded mechanism
 *
 */
void *startNewTranscation(void *socketfd) {
    
    
    // Transaction and stability.
    std::queue<Transaction*> transactions;
    int                 transactionId = -2000;
    char                *filename = NULL;
    std::queue<int>     missing;
    int                 TRANSACTION_METHOD = -1;
    
    // Buffers.
    char                data[INCOMING_BUFFER_LEN];
    char                reply[OUTPUT_BUFFER_LEN];
    
    // Current socket.
    int                 socket = *(int*)socketfd;
    
    
    while(1) {
        Transaction *tx;
        ssize_t     incoming;
        int         index = 0;
        int         len = INCOMING_BUFFER_LEN-1;
        bool        dataFlag = false;
        
        memset(data, 0, INCOMING_BUFFER_LEN);
        memset(reply, 0, OUTPUT_BUFFER_LEN);
        
        // Get data from client byte by byte.
        while( ((incoming = recv(socket, data + index, 1, 0) ) > 0) && (index < (len-1))) {
            index++;
            if( (strstr(data, "\r\n\r\n\r\n")) != NULL) { // No Data
                break;
            } else if( (strstr(data, "\r\n\r\n") != NULL) && (dataFlag != true)) { // Data
                std::string         delimeterData = "\r\n\r\n";
                std::string         tokens[4];
                std::string         dataSoFar(data);
                
                // Get data length.
                for(int i=0; i <= 3; i++) {
                    tokens[i] = dataSoFar.substr(0, dataSoFar.find(" "));
                    dataSoFar.erase(0, tokens[i].length()+1);
                }
                
                // If commit or abort, skip.
                if ( (strcmp(tokens[0].c_str(), "COMMIT") == 0) || (strcmp(tokens[0].c_str(), "ABORT") == 0)) {
                    continue;
                }
                
                int datalen = atoi(&tokens[3][0]);
                
                if(datalen<=0) {
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ERROR", transactionId, 0, 204, strlen("No data sent"), "\r\n\r\nNo data sent");
                    send(socket, reply, strlen(reply), 0);
                    goto CLEANUP;
                }
                
                len = datalen + index;
                dataFlag = true;
            }
        }
        
        tx = parseRequest(data);
        
        // If connection closed, GOTO MONITOR.
        if(incoming == 0) {
            
            if( (dataFlag == true) && (tx->data == NULL) ) {
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ERROR", transactionId, 0, 204, strlen("Missing filename"), "\r\n\r\nMissing filename");
                send(socket, reply, strlen(reply), 0);
                goto CLEANUP;
            } else {
                printf("Connection put on hold. Going to MONITOR.\n");
                goto MONITOR;
            }
        }
        
        
        
        // Check if this message is meant for a different transaction
        if(transactionId == -2000) {                                    // Brand new thread?
            if((tx->METHOD != NEW_TXN) && (tx->METHOD != READ)) {                                 // New transaction?
                if(transactionAllowed[tx->TRANSACTION_ID] == false) {   // Is it registered with our server?
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 201, 0, "\r\n\r\nINVALID TXN ID"); // This is the client's error, send message back
                    send(socket, reply, strlen(reply), 0);
                    
                    goto CLEANUP;
                } else {                                                // Valid transaction.
                    tx->SOCKET_FD = socket;                             // Assign new socket
                    pthread_mutex_lock(&conditionsMutex);
                    
                    reconnectedSocket[tx->TRANSACTION_ID] = tx;         // Put message into critical section
                    pthread_cond_signal(&conditionVars[tx->TRANSACTION_ID]); // Signal other thread there is a new message
                    
                    pthread_mutex_unlock(&conditionsMutex);
                    
                    printf("Messaging other thread. This one is valid\n");
                    return NULL;                                        // This thread can now exit. No need to clean up, give pointer to other thread.
                }
            }
        }
        
        // Check for valid transaction ID.
        // Skip this message and go back to recv() if invalid.
        if(transactionId != -2000) {                                                    // Are we a new thread?
            if( (tx->TRANSACTION_ID != transactionId) && ( (tx->METHOD != NEW_TXN) || (tx->METHOD != READ) )  ) {    // If not, is this a new transaction or is this a valid id?
                // Send ERROR back to server.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 201, 0, "\r\n\r\nInvalid TXN ID");
                send(socket, reply, strlen(reply), 0);
                continue;
            }
        }
        
    RECONNECTED:
        
        switch (tx->METHOD) {
            case NEW_TXN:                                           // New Transaction
                printf("\nProcessing Method: NEW TRANSACTION\n");
                transactionId = getNewTransactionID();
                
                transactionAllowed[transactionId] = true;
                
                // Assign file name for this thread
                filename = tx->data;
                
                // Assign transaction id for this log
                tx->TRANSACTION_ID = transactionId;
                
                // Write to log.
                writeMessageToLog(tx);
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                printf("Sending back: %s\n", reply);
                // Write to RAM.
                transactions.push(tx);
                
                // Initialize the condition variable
                pthread_mutex_lock(&conditionsMutex);
                pthread_cond_init(&conditionVars[transactionId], NULL);
                pthread_mutex_unlock(&conditionsMutex);
                
                continue;
            case READ:                                           // Read
                printf("\nProcessing Method: READ\n");
                
                // Get filename
                filename = tx->data;
                
                if(filename == NULL) {
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ERROR", transactionId, 0, 204, strlen("Missing filename"), "\r\n\r\nMissing filename");
                    send(socket, reply, strlen(reply), 0);
                }
                
            {
                char *readin = readFile(filename);
                
                if(readin == NULL) {
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 205, 0, "\r\n\r\nFILE DOES NOT EXIST");
                    send(socket, reply, strlen(reply), 0);
                    printf("Sending back: %s\n", reply);
                } else {
                    // Send ACK and file back back.
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s%s", "ACK", 0, 0, 0, strlen(readin), "\r\n\r\n", readin);
                    send(socket, reply, strlen(reply), 0);
                    printf("Sending back: %s\n", reply);
                    
                    free(readin);
                }
            }
                
                goto CLEANUP;
            case WRITE:                                           // Write
                printf("\nProcessing Method: WRITE\n");
                // Write to log.
                writeMessageToLog(tx);
                TRANSACTION_METHOD = WRITE;
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                // Write to RAM.
                transactions.push(tx);
                continue;
            case COMMIT:                                           // Commit
                printf("\nProcessing Method: COMMIT\n");
                
                
                
                // Check for missing sequences - don't write to RAM or LOG if invalid
                missing = checkMissingSequences(transactions, tx->SEQUENCE_NUMBER);
                if(missing.size() > 0) {
                    size_t numMissing = missing.size();
                    for(int i=0; i < numMissing; i++) {
                        snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ACK_RESEND", transactionId, missing.front(), 204, strlen("RESEND SEQUENCE"), "\r\n\r\nRESEND SEQUENCE");
                        send(socket, reply, strlen(reply), 0);
                        missing.pop();
                    }
                    printf("Missing sequences. Wait until we rule them all. *have them all\n");
                    continue;
                } else {
                    // Sort messages
                    transactions = sortMessages(transactions);
                    
                    // Write to log.
                    writeMessageToLog(tx);
                    
                    // Write to ram (for checking purposes)
                    transactions.push(tx);
                    
                    // Only reaches here if transaction successfully commited.
                    if (TRANSACTION_METHOD == WRITE) {
                        printf("Writing\n");
                        
                        // Send ACK back.
                        snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                        send(socket, reply, strlen(reply), 0);
                        
                        writeToDisk(transactions);
                        goto CLEANUP;
                    } else {
                        // Send ERROR back.
                        snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ERROR", transactionId, 0, 204, strlen("\r\n\r\nMessage Sequences have caused internal server error. Try again."), "\r\n\r\nMessage Sequences have caused internal server error. Try again.");
                        send(socket, reply, strlen(reply), 0);
                    }
                }
                break;
            case ABORT:                                           // Abort and kill thread
                writeMessageToLog(tx);
                
                // Send ACK back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK", transactionId, 0, 0, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                printf("Transaction aborted\n");
                goto CLEANUP;
            default:
                // Send ERROR back.
                snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 204, 0, "\r\n\r\n\r\n");
                send(socket, reply, strlen(reply), 0);
                
                printf("Invalid Transaction. Closing connection.\n");
                goto CLEANUP;
        } // end of switch.
        
    MONITOR:
        try{
            printf("Waiting until connection reachieved\n");
            
            pthread_mutex_lock(&conditionsMutex);                                  // Lock mutex.
            pthread_cond_wait(&conditionVars[transactionId], &conditionsMutex);    // Wait till client has messaged us.
            
            tx = reconnectedSocket[transactionId];
            if(tx == NULL) { // Get new socket descriptor.
                pthread_mutex_unlock(&conditionsMutex);
                goto CLEANUP;
            }
            
            socket = tx->SOCKET_FD;
            pthread_mutex_unlock(&conditionsMutex);                                 // Start from connection.
            printf("Connection RE Achieved!\n");
            goto RECONNECTED;
        } catch (std::exception e) {
            printf("An error occurred:%s\nRecovering.", e.what());
            goto MONITOR;
        }
        
    } // end of while.
    
CLEANUP:
    
    transactionAllowed[transactionId] = false;
    
CLOSE_CONNECTION:
    printf("-----------connection closed ------------\n");
    sleep(0.5);
    close(socket);
    decrease_connection_sem();
    return NULL;
}







