#include <stdio.h>
#include <string>
#include <map>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <mutex>
#include <arpa/inet.h>
#include "filesystem.h"

/*
 Steps:
 
 getaddrinfo() - fills out addrinfo
 socket() - get the socket file descriptor
 bind() -bind that socket to a port
 listen() - wait for someone to call
 accept() - accept a connection from some client
 send() and recv() - talk to the connection
 
 Tutorial + documentation followed at http://beej.us/guide/bgnet/output/html/multipage/syscalls.html
 */

#define MAX_CONNECTIONS 2
#define TEST_PORT "3490"



/// Creates a server socket and listens for incoming connections to ip:port.
/// This is a blocking call, so call it last!
void setupServerSocket(const char *ip, const char* port) {
    struct sockaddr_storage incomingAddr;
    struct addrinfo serverInfo;
    struct addrinfo *res;
    socklen_t addr_size;
    int sock, incomingSocket;
    char *ipv4;
    
    if(ip == NULL) {
        ipv4 = "127.0.0.1";
    } else {
        memcpy(ipv4, ip, strlen(ip));
    }
    
    
    // Setup socket
    memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.ai_family = AF_INET;
    serverInfo.ai_socktype = SOCK_STREAM;
    
    // Fill out socket information + ports
    getaddrinfo(ip, port, &serverInfo, &res);
    
    // Create socket
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(sock < 0) {
        printf("socket() failed\n");
        exit(-1);
    }
    
    // Bind the socket to listen to the ip+port combo
    if ( bind(sock, res->ai_addr, res->ai_addrlen) < 0) {
        printf("bind() failed\n");
        exit(-1);
    }
    
    // Print out the internet address
    struct in_addr addr;
    addr.s_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr;
    printf("ip address : %s\n", inet_ntoa(addr));
    freeaddrinfo(res);
    
    // Set the socket to listen
    listen(sock, MAX_CONNECTIONS);
    
    addr_size = sizeof(incomingAddr);
    incomingSocket = accept(sock, (struct sockaddr *)&incomingAddr, &addr_size);
    printf("Connection received\n");
}

// Processes request header and puts into transaction header
// Returns a malloc'ed transaction header - must free after use
transaction *parseRequest(char *request) {
    transaction *txn = (transaction*) malloc(sizeof(transaction));
    std::string requestnew = request;
    std::string delimeter = " ";
    std::string delimeterNoData = "\r\n\r\n\r\n";
    std::string delimeterData = "\r\n\r\n";
    std::string tokens[5]; // 5 strings for all variables (including data)
    size_t index;
    
    // Got the first 3 space delimited arguments
    for(int i =0; i <= 2; i++) {
        tokens[i] = requestnew.substr(0, requestnew.find(delimeter));
        requestnew.erase(0, tokens[i].length()+1);
    }
    
    // If no data delimiter found, check for data!
    index = requestnew.find(delimeterData);
    if(index != std::string::npos) {
        tokens[3] = requestnew.substr(0, index);
        requestnew.erase(0, tokens[3].length()+strlen("\r\n\r\n"));
        tokens[4] = requestnew;
        txn->data = &tokens[4][0];
    } else {
        printf("No data in this message\n");
        index = requestnew.find(delimeterNoData);
        tokens[3] = requestnew.substr(0, index);
    }
    
    // Fill out transcation struct

    txn->CONTENT_LEN =atoi(&tokens[3][0]);
    txn->SEQUENCE_NUMBER = atoi(&tokens[2][0]);
    txn->TRANSACTION_ID = atoi(&tokens[1][0]);
    
    if(strcmp(&tokens[0][0], "READ") ==0) {
       txn->METHOD = READ;
    } else if(strcmp(&tokens[0][0], "NEW_TXN")) {
       txn->METHOD = NEW_TXN;
    } else if(strcmp(&tokens[0][0], "WRITE")) {
       txn->METHOD = WRITE;
    } else if(strcmp(&tokens[0][0], "COMMIT")) {
       txn->METHOD = COMMIT;
    } else if(strcmp(&tokens[0][0], "ABORT")) {
       txn->METHOD = ABORT;
    }
    return txn;
}

int main() {
    
    // Initialize file System
    initializeFileSystem("/Users/olegzdornyy/Documents/testfs");
    //char *readData = readFile("a");
    //printf("%s\n", readData);
    //free(readData);
    
    char *sampleCommand = "NEW_TXN -1 0 8\r\n\r\ntestfile";
    
    transaction *txn = parseRequest(sampleCommand);
    printf("Data given is: %s\n", txn->data);
    processTransaction(txn);

    

    
    // Create a socker + listner - this thread is now blocked forever on this call.
    //setupServerSocket(NULL, "3490");
    
    return 0;
    
}
