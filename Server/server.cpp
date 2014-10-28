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


int main() {
    
    // Initialize file System
    initializeFileSystem("/Users/olegzdornyy/Documents/testfs");
    
    
    
    // Create a socker + listner - this thread is now blocked forever on this call.
    //setupServerSocket(NULL, "3490");
    
    return 0;
    
}
