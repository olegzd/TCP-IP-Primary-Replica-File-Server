//
//  Transaction.cpp
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-27.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include "utilities.h"
#include "filesystem.h"
#include <netdb.h>
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
#include <sys/un.h>
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

void writeToFile(const char *fileName, const char *data, size_t len);

/*
 * Global variables.
 *
 */
// Global condition variables.
std::map<int, pthread_cond_t>    conditionVars; // Used to signal a thread with TRANSACTION_ID (int)
pthread_mutex_t                  conditionsMutex; // Mutex that must be locked to access reconnectedSocket
std::map<int, Transaction*>      reconnectedSocket; // Holds the latest Transaction struct it got
int replicaSocket = -1;

void setReplicaSocket(int socketfd) {
    replicaSocket = socketfd;
}

char *getFileSystemPath() {
    return directory;
}

void *recoveryCheck( void *args ) {
    //
    //
    // Check if the log file exists.
    // If not, this is the first time this server has been run in the directory.
    // Create a new one.
    //
    //
    char filePath[sizeof(directory) + sizeof("/.log") +1]; // +1 for EOF
    sprintf(filePath, "%s%s", directory, "/.log");
    std::string temp(".log");
    long messageCount = 0;
    int maxTransactionID = 0;
    if(fileHash.count(REPLICA_LOG_FILE_NAME) <= 0) {
        pthread_mutex_init(&fileHash[REPLICA_LOG_FILE_NAME], NULL);
    }
    
    if(fileHash.count(temp) >0 ) {
        //
        // Read in .log file
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
        
        messageCount = messages.size();
        // Loop through text messages and the latest transaction id's
        for(int message = 0; message < messageCount; message++) {
            
            Transaction *reference;
            reference = parseRequest( messages.front().c_str());
            messages.pop();
            
            // Set new transaction id - doesn't matter if we overshoot, they are our own version of UUID's
            if( reference->TRANSACTION_ID > maxTransactionID) maxTransactionID = reference->TRANSACTION_ID;
            
            transactionHash[reference->TRANSACTION_ID].push(reference); // Push into hash (so we have at least one variable)
            
            // If message is aborted/commited, remove hash.
            if( (reference->METHOD == ABORT) || (reference->METHOD == COMMIT ) ) {
                transactionHash.erase(reference->TRANSACTION_ID);
            }
            free(reference);
        }
        
        // Connect, send, and disconnect.

        free(filebuffer);
        
        // Don't worry about critical section - it won't be competing for anything before we finish this call.
        lastTransactionId = maxTransactionID;
        
    } else { // Create new .log file in this directory
        pthread_mutex_init(&fileHash[LOG_FILE_NAME], NULL);
        logFile = fopen(filePath, "w+");
    }
    
    
    
    return NULL;
}

/*
 * Initializes the file system.
 * Sets up the log, id hash, and condition signalling mutexes.
 * Goes through directory and checks all existing files
 */
void initializeFileSystem(const char* fullPath, char *ip, char * port) {
    DIR         *dir;
    dirent      *file;
    
    
    dir = opendir(fullPath);
    
    pthread_mutex_init(&id_mutex, NULL);
    pthread_mutex_init(&log_mutex, NULL);
    pthread_mutex_init(&conditionsMutex, NULL);
    
    // Check if DIR exists
    if (dir == NULL) {
        printf("File system directory doesn't exist!. Aborting\n");
        exit(-1);
    }
    
    //
    //
    // Check what files exist and add them to the hash table.
    // Initialize mutexes for each file.
    // This dir check catches hidden files (including log)
    //
    //
    while( (file = readdir(dir)) != NULL) {
        // Don't bother with . or .. files
        std::string temp(file->d_name);
        pthread_mutex_init(&fileHash[temp], NULL);
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
 * Writes the transaction to log
 *
 */
void writeMessageToLog( Transaction *tx ) {
    int tid;
    unsigned long startOfID;
    unsigned long endOfID;
    std::string raw (tx->raw);
    std::string del(" ");
    
    // Get the ID
    startOfID = raw.find(del); // Find method
    endOfID = raw.find(del, startOfID+1); // Skip first space
    tid = atoi( raw.substr(startOfID, endOfID).c_str() );
    
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

/*
 * Writes messages to log and messages replica (if it exists) with file to write and data.
 *
 */
void writeToDisk(std::queue<Transaction *> messages) {
    FILE *file;
    std::string fname(messages.front()->data);
    size_t count;
    int bytesWritten = 0;
    std::string written;
    std::string replicaPayload;
    int transactionID = -1;
    
    // Set written data
    replicaPayload.append(fname);
    
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
    
    // Get transaction and free header (NEW_TXN)
    Transaction *temp = messages.front();
    if(temp->METHOD == NEW_TXN) {
        transactionID = temp->TRANSACTION_ID;
        messages.pop();
        free(temp);
    }
    
    count = messages.size();
    
    // Write to file excluding the first and last parts of the array
    for(int i=0; i < count; i++) {
        Transaction *toFree = messages.front();
        
        
        // Check if it's the COMMIT message
        if(toFree->METHOD == COMMIT) {
            if(toFree->raw != NULL) {
                free(toFree->raw);
            }
            break;
        }
        printf("Writing part %i of %lu to file: %s\n", i+1, count-1, toFree->data);
        fprintf(file, "%s", toFree->data);
        
        // Replica houselogging
        written.append(toFree->data);
        bytesWritten += strlen(toFree->data);
        
        messages.pop();
    }
    // Message for backup
    char ID[64];
    sprintf(ID, " %i ", transactionID);
    
    replicaPayload.append(ID);
    replicaPayload.append(written);
    replicaPayload.append("FIN");
   
    // Force flush to disk & close.
    fflush(file);
    fflush(file);
    fclose(file);
    
    pthread_mutex_unlock(&fileHash[fname]);
    
    
    printf("Trying to send  %s to replica.\n", replicaPayload.c_str());
    // Try to message replica with data to write
    if( replicaSocket >= 0 ) {
        // Check if socket is ready for writing
        send(replicaSocket, "READY", strlen("READY"), 0);
        char temp[32];
        long err = recv(replicaSocket, temp, 32, 0);
        
        if(err <= 0) {
            writeToFile(REPLICA_LOG_FILE_NAME, replicaPayload.c_str(), replicaPayload.size());
            printf("Socket conection closed! Will listen for one to come online.\n");
            pthread_t reacher;
            pthread_create(&reacher, NULL, replicaReacher, NULL);
            pthread_join(reacher, NULL);
            return;
        }
        
        if(send(replicaSocket, replicaPayload.c_str(), replicaPayload.size(), 0) <= 0)  {
            replicaSocket = -1;
            printf("Unable to reach replica. Either none connected, or it's crashed\n");
            writeToFile(REPLICA_LOG_FILE_NAME, replicaPayload.c_str(), replicaPayload.size());
            
            pthread_t reacher;
            pthread_create(&reacher, NULL, replicaReacher, NULL);
            pthread_join(reacher, NULL);
        }
    } else {
        printf("Replica socket not initialized!\n");
        writeToFile(REPLICA_LOG_FILE_NAME, replicaPayload.c_str(), replicaPayload.size());
        pthread_t reacher;
        pthread_create(&reacher, NULL, replicaReacher, NULL);
        pthread_join(reacher, NULL);
    }
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
void writeToFile(const char *fileName, const char *data, size_t len) {
    char path[350];
    FILE *file;
    
    sprintf(path, "%s/%s",directory,fileName);
    
    printf("WRITING TO FILE %zu bytes: %s\n",len, path);
    // Get the lock on the file
    pthread_mutex_lock(&fileHash[fileName]);
    
    // Open file with appen d set as its mode
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
    fflush(file);
    fclose(file);
    pthread_mutex_unlock(&fileHash[fileName]);
}


/*
 * Networked transaction thread - primary forks these threads to reply back to client
 */
void *PRIMARY_startNewTranscation(void *socketfd) {
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
        while( ((incoming = recv(socket, data + index, 1, 0) ) > 0) ) {
            index++;
            if( (strstr(data, "\r\n\r\n\r\n")) != NULL) { // No Data
                break;
            } else if( (strstr(data, "\r\n\r\n") != NULL) && (dataFlag == false)) { // Data
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
            } else if( (dataFlag == true) && (index > (len-1))) {
                index = 0;
                dataFlag=false;
                break;
            }
        }
        
        tx = parseRequest(data);
        memset(data, 0, INCOMING_BUFFER_LEN);
        
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
                    if((tx->METHOD == COMMIT) && (tx->TRANSACTION_ID <= getBiggestTransactionID())){
                        snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK",tx->TRANSACTION_ID , 0, 0, 0, "\r\n\r\n\r\n");
                        send(socket, reply, strlen(reply), 0);
                        goto CLEANUP;
                    } else {
                    
                        snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ERROR", transactionId, 0, 201, 0, "\r\n\r\nINVALID TXN ID"); // This is the client's error, send message back
                        send(socket, reply, strlen(reply), 0);
                    
                        goto CLEANUP;
                    }
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
        if(transactionId != -2000) {                                                                                    // Are we a new thread?
            if( (tx->TRANSACTION_ID != transactionId) && ( (tx->METHOD != NEW_TXN) || (tx->METHOD != READ) )  ) {    // If not, is this a new transaction or is this a valid id?
                // Is it a COMMIT asking for reacknowledgement?
                if((tx->METHOD == COMMIT) && (tx->TRANSACTION_ID <= getBiggestTransactionID())){
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %i %s", "ACK",tx->TRANSACTION_ID , 0, 0, 0, "\r\n\r\n\r\n");
                    goto CLEANUP;
                }
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
                
                if(tx->SEQUENCE_NUMBER <= 0) {  // Sequence with a bad sequence number?
                    snprintf(reply, OUTPUT_BUFFER_LEN, "%s %i %i %i %zu %s", "ERROR", transactionId, 0, 204, strlen("Invalid sequence"), "\r\n\r\nInvalid sequence");
                    send(socket, reply, strlen(reply), 0);
                    free(tx);
                    continue;
                }
                
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
/*
 *  Replica server thread. It's a single threaded mechanism so it's the only one ever spooled.
 *  QUICK NOTE TO PROTECT MY SELF FROM PLAGIARISM: 
 *  some of the socket bits were taken from my CMPT 471 class which followed
 *  http://beej.us/ 's tutorial.
 *
 *  Acts as a server that takes commands from the primary server
 */
void *BACKUP_service(void *arguments) {
    ReplicaArgs args = *(ReplicaArgs*)arguments;
    char cbuffer[INCOMING_BUFFER_LEN];
    std::string buffer;
    long incoming;
    int index = 0;
    std::map<int, std::queue<Transaction *>> transactions;
    int sock = args.psocket;
    std::string receivedString;
    
    printf("Spooling backup service thread.\n");
    
    // Get the first part of the connection, then we're good to go
    incoming = recv(sock, cbuffer + index, 1, 0);
    index++;
    
    // BACKUP thread loop - if this thread returns, means the primary is down
    while(incoming > 0) {
        // Get data from client byte by byte.
        while( ((incoming = recv(sock, cbuffer + index, 1, 0) ) > 0) && (index < INCOMING_BUFFER_LEN) ) {
            index++;
            if( (strstr(cbuffer, "FIN") != NULL)) { // END OF DATA
                break;
            } else if (strstr(cbuffer, "MASTER_FAILURE") != NULL) {
                // Master has crashed - assume master position
                printf("REPLICA: Master failure detecting - returning");
                return NULL;
            } else if (strstr(cbuffer, "READY") != NULL) {
                send(sock, "READY", strlen("READY"), 0);
                memset(cbuffer, 0, INCOMING_BUFFER_LEN);
                index = 0;
                continue;
            }
        }
        
        index = 0;
        
        buffer = std::string(cbuffer);
        printf("BUFFER: %s\n", buffer.c_str());
        
        // Find first space - all characters leading up to it is the filename
        int poistionFilename = (int)buffer.find(" ");
        std::string fileName = buffer.substr(0, poistionFilename);
        printf("Filename: %s\n", fileName.c_str());
        
        // Get rid of filename
        buffer = buffer.substr(poistionFilename+1);
        
        // Get transaction id
        long positionToWrite = buffer.find(" ");
        int transactionID = atoi(buffer.substr(0, positionToWrite).c_str());
        printf("Transaction: %i\n", transactionID);
        
        // Get data to write minus the FIN keyword
        buffer = buffer.substr(positionToWrite+1);
        buffer = buffer.substr(0, buffer.size()-3);
        
        // Write to our file system
        writeToFile(fileName.c_str(), buffer.c_str(), buffer.size());
        
        memset(cbuffer, 0, INCOMING_BUFFER_LEN);
    }
    
    printf("Exiting replica mode\n");
        
    // Primary crashed if reached here
    return NULL;
}






