//
//  recoveryClient.c
//  Server
//
//  Created by Oleg Zdornyy on 2014-11-02.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#include "recoveryClient.h"
#include <sys/socket.h>
#include <string.h>
#include <string>
#include <pthread.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <queue>
#include <map>

#include "filesystem.h"
#include "server.h"

/*
 Tutorial followed at http://beej.us/guide/bgnet/output/html/multipage/syscalls.html
 */

void *recoveryClient(void *transactions) {
    struct addrinfo serverInfo;
    struct addrinfo *res;
    int sock;
    char ipv4[20];
    char portaddr[6];

    std::map<int, std::queue<Transaction *> > transactionHash = *(std::map<int, std::queue<Transaction *> >*) transactions;
    typedef std::map<int, std::queue<Transaction *> >::iterator transactionIterator;
    
    pthread_t respawnThreads[transactionHash.size()];
    
    if(transactionHash.size() <= 0) { // No need to recover
        printf("No log file - skipping recovery\n");
        return NULL;
    }

    // Initialize Client
    strlcpy(ipv4, getServerIP(), 20);
    strlcpy(portaddr, getServerPort(), 6);

    // Setup socket
    memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.ai_family = AF_INET;
    serverInfo.ai_socktype = SOCK_STREAM;
    
    // Fill out socket information + ports
    getaddrinfo(ipv4, portaddr, &serverInfo, &res);
    
    if(res == NULL) {
        printf("Unable to recover. Starting clean slate\n");
        return NULL;
    }
    
    // Create socket
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(sock < 0) {
        printf("Recovery Client failed. Skipping recovery.\n");
        return NULL;
    }
    
    // Print out the internet address
    struct in_addr addr;
    addr.s_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(res);
    
   // sleep(3); // Sleep for 200 milliseconds to give the main server loop time to init.
    
    connect(sock, res->ai_addr, res->ai_addrlen);
    
    transactionIterator it =  transactionHash.begin();
    printf("Loop pre-entry\n");
    int currentTransactionIndex = 0;
    
    while( it != transactionHash.end() ) {
        
        pthread_create(&respawnThreads[currentTransactionIndex], NULL, startNewTranscation, (void*)&sock);
        
        
        sleep(0.1);
        
        
        // Iterate through this transaction queue
        int queueSize = it->second.size();
        for(int i =0; i < queueSize; i++) {
            printf("SENDING OFF: %s\n", it->second.front()->raw);
            send(sock, it->second.front()->raw, strlen(it->second.front()->raw), 0);
            sleep(0.5);
            free(it->second.front()->raw);
            free(it->second.front()->data);
            it->second.pop();
        }
        
        currentTransactionIndex++;
        it ++;
    }
    
    close(sock);
    
    printf("RECOVERY COMPLETE - SERVER BACK TO LAST STATE.\n");
    return NULL;
    
}