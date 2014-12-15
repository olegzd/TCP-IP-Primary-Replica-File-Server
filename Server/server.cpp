#include <unistd.h>
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
#include <iostream>
#include <sys/un.h>
#include <time.h>
#include <fstream>
#include <netdb.h>


#include "server.h"
/*
 Tutorial followed at http://beej.us/guide/bgnet/output/html/multipage/syscalls.html
 */


#define         MAX_CONNECTIONS 256
#define         TIME_FILE_PATH   "/tmp/301148294time"
#define         LOGGING_INTERVAL 1
#define         PRIMARY_SECONDARY_PORT "3000"

pthread_t       threads[MAX_CONNECTIONS]; // MAX 32 Connections - otherwise hold
sem_t           threadSem;
int             threadCount=256;
pthread_mutex_t threadCountMutex;
int             BACKUP_UNIX_SOCKET = -1;
pthread_mutex_t replicaReacherInitiated;
bool            primaryReplcaSocketInit = false;
int             replicaSocketListenerPort = 0;

int connectToPrimary(std::string ip, std::string port);
bool connectionAlreadyExists(char *message);
bool checkValidTransactionID(char *message);
void *serverMonitor( void *args );
void *replicaServer(void *args);
void *timeLogger(void *args);
void *replicaReacher(void *args);
char serverip[20];
char serverport[6];

typedef struct Arguments Arguments;
struct Arguments {
    char *ip;
    char *port;
    char *primaryDir;
    bool primary = true;
    char *nfs;
};

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

/*
 * Checks if a file exists
 * If exists, return 1
 * If does not exist, returns 0
 */
int checkFileExistance(std::string path) {
    if(access(path.c_str(), F_OK) != -1) {
        return 1;
    } else {
        return 0;
    }
}

/********************************************************************************************************
 *
 * General Server (primary & backup) related functions
 *
 ********************************************************************************************************/


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

SECONDARY:
    if(args.primary != true) { // We are a replica.
        char readIn[64];
        size_t point;
        std::string pip;
        std::string pport;
        
        // Get the primary's ip & port from the nfs arg.
        FILE *primarytxt = fopen(args.nfs, "r");
        if(primarytxt == NULL) {
            printf("Unable to access primary.txt file. Aborting\n");
            exit(-1);
        }
        
        // Read primary.txt
        fread(readIn, sizeof(char), 64, primarytxt);
        std::string cppReadin = std::string(readIn);
        
        // Figure out the primary ip
        point = cppReadin.find(" ");
        pip = cppReadin.substr(0, point);
        
        // Cutoff the ip part of the string
        cppReadin = cppReadin.substr(0, point+1);
        
        // Figure out primary port
        pport = PRIMARY_SECONDARY_PORT;
       
        // Try to reach primary. Exits on failure.
        ReplicaArgs replica_args;
        int primarySocket = connectToPrimary(pip, pport);
        replica_args.psocket = primarySocket;
        
        // Fork off replica. Continue to act as replica until primary is down.
        pthread_t replica;
        pthread_create(&replica, NULL, BACKUP_service, &replica_args);
        pthread_join(replica, NULL);
        
        printf("Primary has come offline - switching to primary mode\n");
    }
    
PRIMARY:
    //
    // IF HERE, WE ARE STARTING AS A PRIMARY, OR REPLICA IS SWITCHING TO PRIMARY
    //
    pthread_t timeThread, replicaPingThread;
    
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
    printf("Master online\n");
    printf("ip address : %s\n", inet_ntoa(addr));
    printf("port: %s\n", portaddr);
    freeaddrinfo(res);
    
    // Set the socket to listen
    listen(sock, MAX_CONNECTIONS);
    addr_size = sizeof(incomingAddr);
    
    // Everything is good so far, fork off thread
    //pthread_create(&timeThread, NULL, timeLogger, NULL);
    
    FILE *primary = fopen(args.nfs, "w+");
    if(primary != NULL) {
        // Write info to primary.txt
        printf("Writing %s %s to primary.txt\n", args.ip, args.port);
        fprintf(primary, "%s %s", args.ip, args.port);
        fclose(primary);
        
        // Thread that listens for any replicas to connect to it.
        pthread_create(&replicaPingThread, NULL, &replicaReacher, &args);
    }
    
    
    
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
            
            pthread_create(&threads[threadIndex], NULL, PRIMARY_startNewTranscation, (void*)incomingSocket);
        }
    }
    // End of primary server intialization.
    return NULL;
}

/********************************************************************************************************
 *
 * PRIMARY Server related functions
 *
 ********************************************************************************************************/

// Function that tries to setup a connection with a replica
// Exits when a connection is achieved after setting file system's replica socket
void flushBacklogToReplica(int socket);
void *replicaReacher(void *args) {
    struct 		sockaddr_storage incoming;
    struct 		addrinfo serverInfo;
    struct 		addrinfo *result;
    socklen_t 	addr_size;
    int 		replicaSocketListener = 0;

    // Init primary-replica socket
    if(primaryReplcaSocketInit == false) {
        
        // Setup socket
        memset(&serverInfo, 0, sizeof(serverInfo));
        serverInfo.ai_family = AF_INET;
        serverInfo.ai_socktype = SOCK_STREAM;
        
        // Fill out socket info. THIS OPENS A DIFFERENT PORT JUST FOR REPLICAS (3000) BUT ON SAME IP.
        getaddrinfo(serverip, PRIMARY_SECONDARY_PORT, &serverInfo, &result);
        
        // Create socket
        replicaSocketListenerPort = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if(replicaSocketListenerPort < 0) {
            printf("Failed to create primary's replica socket. Replicas are unable to connect to this primary. %s\n", strerror(errno));
            return NULL;
        }
        
        struct timeval timeout;
        timeout.tv_usec = 500;
        int val = 1;
        setsockopt(replicaSocketListenerPort, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val);
        setsockopt(replicaSocketListenerPort, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        // Bind socket
        if ( bind(replicaSocketListenerPort, result->ai_addr, result->ai_addrlen) < 0) {
            printf("Failed to bind socket.  Replicas are unable to connect to this primary\n");
            return NULL;
        }
        // Set to listen
        listen(replicaSocketListenerPort, 1);
        primaryReplcaSocketInit = true;
    }
    
    // Try to acquire lock. If already acquired, means someone else is trying to reach replica
    if( 0 != pthread_mutex_trylock(&replicaReacherInitiated)) {
        return NULL;
    }
    printf("Trying to connect to replica\n");
    
    // Replica has connected to us, set the filesystem flag.
    int newSocket = accept(replicaSocketListenerPort, (struct sockaddr *)&incoming, &addr_size);
    
    // Once replica is online, check if there are files needed to be written out
    flushBacklogToReplica(newSocket);
    
    printf("Replica connected!\n");
    
    setReplicaSocket(newSocket);
    pthread_mutex_unlock(&replicaReacherInitiated);
    return NULL;
}


void flushBacklogToReplica(int socket) {
    // Get the directory path
    char *dirpath = getFileSystemPath();
    char fullReplicaLogPath[256];
    
    sprintf(fullReplicaLogPath, "%s/%s", dirpath, REPLICA_LOG_FILE_NAME);
    
    FILE *file = fopen(fullReplicaLogPath, "a+");
    if(file==NULL) {
        printf("Unable to open replica propogation log.\n");
        return;
    }
    
    fseek(file, 0, SEEK_END);        // Get end of file
    size_t filesize = ftell(file);   // Get size of file
    rewind(file);                    // Rewind to beginning
    
    char *filebuffer = (char*) calloc(1, filesize);
    size_t readin = fread(filebuffer, 1, filesize, file);
    
    // Error check and recover
    if(readin != filesize) {
        printf("Error trying to propogate changes to replica! This file system will now act as if clean state\n.");
        fclose(file);
        return;
    }
    
    if(readin == 0) {
        printf("Replication log is empty\n");
    }
    
    std::string delim = std::string("FIN");
    std::string buffer = std::string(filebuffer);
    
    while(buffer.size() >0) {
        printf("Buffer size to propogate:%i\n", buffer.size());
        int index = 0;
        
        index = buffer.find(delim);
        std::string toWrite = buffer.substr(0,index-1); // -1 removes the zero at the end of the str
        toWrite.append("FIN");
        
        send(socket, toWrite.c_str(), toWrite.size(), 0);
        if(buffer.size() <= index+4){
            break;
        } else {
            buffer = buffer.substr(index+4);
        }
    }
    
    // Free malloced memory, and truncate the file
    free(filebuffer);
    fclose(file);
    
    // This opens the file as if it's new and then we close it again
    file = fopen(fullReplicaLogPath, "w+");
    fclose(file);
}

/*
 * Writes time (in seconds from epoch) to a file in TIME_FILE_PATH.
 * Removes entries after the 10th entry and starts over.
 * Writes at 2 second intervals
 */
void *timeLogger(void *args) {
    FILE *file;
    time_t timer;
    file = fopen(TIME_FILE_PATH, "w+");
    char buffer[30];
    int loops = 0;
    
    // Write current time (in seconds)
    // Remove after 10
    while(1) {
        loops++;
        timer = time(NULL);
        
        sprintf(buffer, "%ld\n",timer);
        fwrite(buffer, sizeof(char), strlen(buffer), file);
        
        fflush(file);
        
        sleep(LOGGING_INTERVAL);
        
        memset(buffer, 0, 30);
        
        if(loops > 10) {
            fclose(file);
            file = fopen(TIME_FILE_PATH, "w+");
            loops=0;
        }
    }
    
    // Should never get here
    return NULL;
}

/********************************************************************************************************
 *
 * REPLICA Server related functions
 *
 ********************************************************************************************************/
int connectToPrimary(std::string ip, std::string port) {
    printf("Attempting to reach primary at : %s:%s\n", ip.c_str(), port.c_str());
    
    struct 		addrinfo serverInfo;
    struct 		addrinfo *result;
    int sock;
    
    // Setup socket
    memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.ai_family = AF_INET;
    serverInfo.ai_socktype = SOCK_STREAM;
    
    // Fill out socket information + ports
    getaddrinfo(ip.c_str(), port.c_str(), &serverInfo, &result);
    
    // Create socket
    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(sock < 0) {
        printf("Failed to create socket().\n");
        exit(-1);
    }

    // Try to connect
    if( connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
        printf("Unable to connect to primary. %s\n", strerror(errno));
        exit(0);
    }
    
    return sock;
}


// Monitors that PRIMARY is online
void *serverMonitor( void *args ) {
    // Open time file
    std::fstream file;
    file.open(TIME_FILE_PATH);
    char lastTimeEntry[15];
    time_t timenow;
    bool flag = false;
    
    // Error check
    if(file.is_open() == false) {
        printf("There is corruption in the time log. Initializing primary server\n");
    }
    
    memset(lastTimeEntry, 0, 15);
    
    for( ;; ) {
        timenow = time(NULL);
        
        // Go to end of file - 11 (the string length)
        file.seekg(-11, std::ios_base::end);
        
        file.getline(lastTimeEntry,  12);
        
        // Being half a line + previous one: last entry was cut off - master is down
        if(strstr(lastTimeEntry, "\n") != NULL) {
            if(flag == false) {
                flag = true;
                sleep(1);
                continue;
            }
            printf("there exists a newline char\n");
            break;
        }
        
        // Last entry was cut off - master down
        if(strlen(lastTimeEntry) <10) {
            if(flag == false) {
                flag = true;
                sleep(1);
                continue;
            }
            break;
        }
        
        // If server hasn't updated in 2 seconds - master is down
        if( (timenow - atoi(lastTimeEntry) ) > 2 ) {
            break;
        }
        
        sleep(LOGGING_INTERVAL + 1);
        memset(lastTimeEntry, 0, 15);
    }

    // If we got to here, the master is down.
    // Message replica thread to take over as master.
    printf("Master is down - attempting to switch to master\n");
    if( BACKUP_UNIX_SOCKET > 0) send(BACKUP_UNIX_SOCKET, "MASTER_FAILURE", strlen("MASTER_FAILURE"), 0);
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




// Required to get the -dir argument! port and ip is optional
// All arguments must be preceded by --
// (eg --dir /dir/dir)
int main(int argc, char *argv[]) {
    char *dir = NULL;
    char *ip = NULL;
    char *port = NULL;
    char *type = NULL;
    char *nfs = NULL;
    Arguments arguments;
    
    
    /* -- Get command line arguments --*/
    static struct option options[] = {
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"dir", required_argument, NULL, 'd'},
        {"type", required_argument, NULL, 't'},
        {"nfs", required_argument, NULL, 'n'},
        {NULL, 0, NULL, 0}
    };
    
    while(1) {
        int c = getopt_long(argc, argv, "d:i:p:t:n:", options, NULL);
        
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
        
        if(c == 't') { // server type (primary or replica)
            type = optarg;
        }
        
        if(c == 'n') { // server type (primary or replica)
            nfs = optarg;
        }

        if(c == -1) { // no arguments left
            break;
        }
        
    }
    
    // Check to make sure we have valid arguments
    if(dir == NULL) {
        printf("No directory argument found! Aborting\n");
        return -1;
    } else {
        arguments.primaryDir = dir;
    }
    if(type == NULL) {
        printf("No server type specified! Aborting\n");
        return -1;
    } else {
        if(strstr(type, "primary") != NULL) arguments.primary = true;
        else if(strstr(type, "replica") != NULL) arguments.primary = false;
        else {
            printf("Invalid server type. Must be 'replica' or 'primary'. Aborting\n");
            exit(1);
        }
    }
    if(nfs == NULL) {
        printf("No network file system path specified! Aborting\n");
        return -1;
    } else {
        arguments.nfs = nfs;
    }
    
    if(port == NULL) {
        port = (char*)"8080";
    }
    
    if(ip == NULL) {
        ip = (char*)"127.0.0.1";
    }
    
    
    
    // Initialize semaphore & mutexes
    sem_init(&threadSem,0, 32);
    pthread_mutex_init(&threadCountMutex, NULL);
    pthread_mutex_init(&replicaReacherInitiated, NULL);
    
    // Create a socket + listner.
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
