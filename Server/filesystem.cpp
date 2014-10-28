//
//  transaction.cpp
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-27.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include "filesystem.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <mutex>
#include <map>

/// Structure that will hold the information necessary for a transacton ID
std::map <char*, pthread_mutex_t> fileHash;
char directory[256];
char *readFile(char *fileName);

void initializeFileSystem(const char* fullPath) {
    DIR *dir = opendir(fullPath);
    dirent *file;
    
    // Check what files exist and add them to the hash table - initialize mutexes for each file
    while( (file = readdir(dir)) != NULL) {
        // Don't bother with . or .. files
        if( strncmp(file->d_name, "..", sizeof("..")) && strncmp(file->d_name, ".", sizeof(".")) ) {
            pthread_mutex_init(&fileHash[file->d_name], NULL);
        }
    }
    closedir(dir);
    
    strncpy(directory, fullPath, sizeof(directory));
    printf("File system initialized\n");
}

// Handle transaction
char *processTransaction(transaction *txn) {
    switch (txn->METHOD) {
        case NEW_TXN:
            
            break;
        case READ:
            return readFile(txn->data);
            break;
        case WRITE:

            break;
        case COMMIT:
            
            break;
        case ABORT:
            
            break;
        default:
            printf("Invalid transaction\n");
            return NULL;
    }
    
    return NULL;
}

// Read a file from directory/filename - returns either null or a malloc'ed string
char *readFile(char *fileName) {
    try {
        struct stat st;
        char path[350];
        FILE *file;
        size_t fileSize;
        
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
        data[fileSize] = '\0';
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
    
    // Open file with append set as its mode
    file = fopen(path, "a");
    if(file == NULL) {
        printf("Error opening file \"%s\" . %s\n", path, strerror(errno));
        return;
    }
    
    // All is good, time to write to file
    if( fwrite(data, 1, len, file) < len ) {
        printf("Error writing to %s\n", fileName);
        return;
    }
}













