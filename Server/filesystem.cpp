//
//  transaction.cpp
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-27.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include "filesystem.h"


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

char *readFile(char *fileName) {
    try {
        struct stat st;
        char *path;
        FILE *file;
        size_t fileSize;
        
        // Lock the file mutex (block if needed)
        pthread_mutex_lock(&fileHash[fileName]);
        
        // Get the path
        sprintf(path, "%s/%s",directory,fileName);
        
        
        // Open file, get size, and read it - since we're not worrying about writing to it, use stack
        file = fopen(path, "r");
        
        stat(fileName, &st);
        fileSize = st.st_size;
        
        char *data = (char*)malloc(fileSize+1);
        
        if(fileSize < fread(data, 1, fileSize, file) ) {
            printf("Error reading file %s\n", fileName);
            free(data);
            return NULL;
        }
        data[fileSize+1] = '\0';
        printf("%s\n",data);
        return data;
    } catch (std::exception e) {
        printf("Cannot open file %s. %s\n", fileName, e.what());
        return NULL;
    }
}

