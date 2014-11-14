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
#include <semaphore.h>
#include <signal.h>
#include <cstring>


#include "server.h"
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


#define         MAX_CONNECTIONS 2
#define         TEST_PORT "3490"



pthread_t       threads[32]; // MAX 32 Connections - otherwise hold
sem_t           threadSem;
int             threadCount=32;
pthread_mutex_t threadCountMutex;

bool connectionAlreadyExists(char *message);
bool checkValidTransactionID(char *message);

typedef struct Arguments Arguments;
struct Arguments {
    char *ip;
    char *port;
};

char serverip[20];
char serverport[6];

char *getServerPort() {
    return serverport;
}

char *getServerIP() {
    return serverip;
}



// catches broken pipes
void catch_signal(int sig) {
    printf("Caught bad signal: %d\n", sig);
}



/// Creates a server socket and listens for incoming connections to ip:port.
/// This is a blocking call, so call it last!
void *setupServerSocket(void *arguments) {
    struct sockaddr_storage incomingAddr;
    struct addrinfo serverInfo;
    struct addrinfo *res;
    socklen_t addr_size;
    int sock;
    char ipv4[20];
    char portaddr[6];
    char incomingBuf[4096];
    
    // Get args.
    Arguments args = *(Arguments *)arguments;
    
    // Initialize Server
    if(args.ip == NULL) {
        strcpy(ipv4, "127.0.0.1");
        strcpy(serverip, "127.0.0.1");
    } else {
        strcpy(ipv4, args.ip);
        strcpy(serverip, args.ip);
    }
    if(args.port == NULL) {
        strcpy(portaddr, "8080");
        strcpy(serverport, "8080");
    } else {
        strcpy(portaddr, args.port);
        strcpy(serverport, args.port);
    }
    
    
    // Setup socket
    memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.ai_family = AF_INET;
    serverInfo.ai_socktype = SOCK_STREAM;
    
    // Fill out socket information + ports
    getaddrinfo(ipv4, serverport, &serverInfo, &res);
    
    // Create socket
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    
    int option = 1;
    setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, &option, sizeof option);
    
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
    printf("port: %s\n", portaddr);
    freeaddrinfo(res);
    
    // Set the socket to listen
    listen(sock, MAX_CONNECTIONS);
    
    addr_size = sizeof(incomingAddr);
    
    // Receive connections.
    while(1) {
        int *incomingSocket = (int*)calloc(1, sizeof(*incomingSocket));
        
        *incomingSocket = accept(sock, (struct sockaddr *)&incomingAddr, &addr_size);
        
        if(incomingSocket < 0) {
            printf("Error - could not resolve new socket!\n");
            free(incomingSocket);
        } else {
            printf("Forking off new thread!\n");
            int threadIndex = decrease_connection_sem();
            
            pthread_create(&threads[threadIndex], NULL, startNewTranscation, (void*)incomingSocket);
        }
    }
    
    return NULL;
}

int decrease_connection_sem() {
    
    int threadIndex = -1;
    sem_wait(&threadSem);
    
    // Critical section (I just like saying that :) )
    pthread_mutex_lock(&threadCountMutex);
    threadIndex = threadCount;
    threadCount--;
    pthread_mutex_unlock(&threadCountMutex);
    
    return threadIndex;
}

void post_connection_sem() {
    sem_post(&threadSem);
    pthread_mutex_lock(&threadCountMutex);
    threadCount++;
    pthread_mutex_unlock(&threadCountMutex);
}

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


// Processes request header and puts into Transaction header
// Returns a malloc'ed Transaction header - must free after use
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
        return -1;
    }
    
    if(port == NULL) {
        port = (char*)"8080";
    }
    
    if(ip == NULL) {
        ip= (char*)"127.0.0.1";
    }
    
    // Run tests
    //runTest();
    
    // Initialize semaphore & mutexes
    sem_init(&threadSem,0, 32);
    pthread_mutex_init(&threadCountMutex, NULL);
    
    
    // Create a socket + listner.
    // This thread is now listening from this call
    
    Arguments arguments;
    arguments.ip = ip;
    arguments.port = port;
    
    
    // Create server thread
    pthread_t server;
    pthread_t recoveryClient;
    
    
    // Initialize File System
    initializeFileSystem(dir, ip, port);
    
    pthread_create(&server, NULL, &setupServerSocket, (void *)&arguments);
    pthread_create(&recoveryClient, NULL, &recoveryCheck, (void*)NULL);
    
    signal(SIGPIPE, catch_signal);
    
    pthread_join(recoveryClient, NULL);
    pthread_join(server, NULL);
    
    return 0;
    
}
