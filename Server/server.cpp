#include <stdio.h>
#include <string>
#include <map>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <mutex>
#include <arpa/inet.h>
#include <getopt.h>
#include "filesystem.h"

/*
 Steps:
 
 getaddrinfo() - fills out addrinfo
 socket() - get the socket file descriptor
 bind() -bind that socket to a port
 listen() - wait for someone to call
 accept() - accept a connection from some client
 send() and recv() - talk to the connection
 
 Tutorial followed at http://beej.us/guide/bgnet/output/html/multipage/syscalls.html
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

// Processes request header and puts into Transaction header
// Returns a malloc'ed Transaction header - must free after use
Transaction *parseRequest(const char *request) {
    Transaction *txn = (Transaction*) malloc(sizeof(Transaction));
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
    
    txn->SEQUENCE_NUMBER = atoi(&tokens[2][0]);
    txn->TRANSACTION_ID = atoi(&tokens[1][0]);
    
    // If no data delimiter found, check for data!
    index = requestnew.find(delimeterData);
    if(index != std::string::npos) {
        tokens[3] = requestnew.substr(0, index);
        requestnew.erase(0, tokens[3].length()+strlen("\r\n\r\n"));
        
        // Get length of datafile to allocate enough memory
        txn->CONTENT_LEN =atoi(&tokens[3][0]);
        tokens[4] = requestnew;
        
        // malloc and copy over the string
        txn->data = (char *)malloc((size_t)txn->CONTENT_LEN+1);
        
        strlcpy(txn->data, &tokens[4][0], txn->CONTENT_LEN+1);
    } else {
        printf("No data in this message\n");
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
    txn->raw = (char*)malloc(totalSize);
    strlcpy(txn->raw, request, totalSize);
    strcat(txn->raw, "\n");
    
    return txn;
}

void runTest() {
     printf("\n\n********STARTING NEW TEST (write) ***********\n\n");
    const char *sampleRequest = "NEW_TXN -1 0 6\r\n\r\nlegion";
    const char *sampleRequest2 = "WRITE 0 1 8\r\n\r\nSuccess1";
    const char *sampleRequest3 = "WRITE 0 2 8\r\n\r\nSuccess2";
    const char *sampleRequest4 = "COMMIT 0 2 0\r\n\r\n\r\n";
    Transaction *txn1 = parseRequest(sampleRequest);
    Transaction *txn2 = parseRequest(sampleRequest2);
    Transaction *txn3 = parseRequest(sampleRequest3);
    Transaction *txn4 = parseRequest(sampleRequest4);
    Transaction *array[] = {txn1, txn3, txn2, txn4};
    //startFakeNewTranscation(array,4);
    
    //printf("\n\n********STARTING NEW TEST (read)***********\n\n");
    
    const char *sampleRequestd = "NEW_TXN -1 0 3\r\n\r\nbcd";
    const char *sampleRequest2d = "READ 0 1 3\r\n\r\nbcd";
    const char *sampleRequest3d = "COMMIT 0 1 0\r\n\r\n\r\n";
    Transaction *txn1d = parseRequest(sampleRequestd);
    Transaction *txn2d = parseRequest(sampleRequest2d);
    Transaction *txn3d = parseRequest(sampleRequest3d);
    Transaction *arrayd[] = {txn1d, txn2d, txn3d};
    
    //startFakeNewTranscation(arrayd, 3);
    
    printf("\n\n********STARTING NEW TEST (long write - unsorted)***********\n\n");
    
    const char *sampleRequestb = "NEW_TXN -1 0 6\r\n\r\ngarrus";
    const char *sampleRequest2b = "WRITE 0 1 8\r\n\r\nSuccess1";
    const char *sampleRequest3b = "WRITE 0 2 8\r\n\r\nSuccess2";
    const char *sampleRequest4b = "WRITE 0 3 8\r\n\r\nSuccess3";
    const char *sampleRequest5b = "WRITE 0 4 8\r\n\r\nSuccess4";
    const char *sampleRequest6b = "WRITE 0 5 8\r\n\r\nSuccess5";
    const char *sampleRequest7b = "WRITE 0 6 8\r\n\r\nSuccess6";
    const char *sampleRequest8b = "WRITE 0 7 8\r\n\r\nSuccess7";
    const char *sampleRequest9b = "COMMIT 0 7 0\r\n\r\n\r\n";
    Transaction *txn1b = parseRequest(sampleRequestb);
    Transaction *txn2b = parseRequest(sampleRequest2b);
    Transaction *txn3b = parseRequest(sampleRequest3b);
    Transaction *txn4b = parseRequest(sampleRequest4b);
    Transaction *txn5b = parseRequest(sampleRequest5b);
    Transaction *txn6b = parseRequest(sampleRequest6b);
    Transaction *txn7b = parseRequest(sampleRequest7b);
    Transaction *txn8b = parseRequest(sampleRequest8b);
    Transaction *txn9b = parseRequest(sampleRequest9b);
    Transaction *arrayb[] = {txn1b, txn8b, txn6b, txn7b, txn5b, txn2b, txn3b, txn4b, txn9b};
    
    startFakeNewTranscation(arrayb, 9);
    
}

// Required to get the -dir argument! port and ip is optional
// All arguments must be preceded by --
// (eg --dir /dir/dir)
int main(int argc, char *argv[]) {
    char *dir = NULL;
    char *ip = NULL;
    char *port = NULL;
    
    /* -- Get command line arguments --*/
    static struct option options[] = {
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"dir", required_argument, NULL, 'd'},
        {NULL, 0, NULL, 0}
    };

    while(1) {
        int c = getopt_long(argc, argv, "d:i:p:", options, NULL);
        
        // Last character can only be r for dir, p for ip, or t for port
        if(c == 'd') { // dir argument
            dir = optarg;
        }
        
        if(c == 'p') { // port argument
            port = optarg;
        }
        
        if(c == 'i') { // ip argument
            ip = optarg;
        }
        
        if(c == -1) { // no arguments left
            break;
        }
    }
    
    // Check to make sure we have dir as an argument
    if(dir == NULL) {
        printf("No directory argument found! Aborting\n");
    }
    
    // Initialize the server
    initializeFileSystem(dir, ip, port);
    
    // Run tests
    runTest();
    
    
    
    // Create a socker + listner - this thread is now blocked forever on this call.
    //setupServerSocket(NULL, "3490");
    
    return 0;
    
}
