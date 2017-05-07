#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#define PORT "2000"
#define MAX_BUF 200

#define ERR(source) ( fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                        perror(source), exit(EXIT_FAILURE) )

#define MSG_LOGIN "#login"
#define MSG_LOGOUT "#logout"

#define MSG_PING "#ping"
#define MSG_PONG "#pong"
#define MSG_CLOSED "#closed"

void usage(char *fileName)
{
    fprintf(stderr, "Usage: %s\n", fileName);
    exit(EXIT_FAILURE);
}

void setSignalHandler(int signal, void (*handler)(int))
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = handler;
    if (sigaction(signal, &action, NULL) < 0)
        ERR("sigaction");
}

volatile sig_atomic_t shouldQuit = 0;
void handleSigInt(int signal)
{
    shouldQuit = 1;
}

struct sockaddr_in makeAddress(char *address, char *port)
{
    int errorCode = 0;
    struct sockaddr_in udpAddress;
    struct addrinfo *addressInfo;
    struct addrinfo hints = {};
    
    memset(&udpAddress, 0, sizeof(struct sockaddr_in));
    hints.ai_family = AF_INET;
    if((errorCode = getaddrinfo(address, port, &hints, &addressInfo)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(errorCode));
        exit(EXIT_FAILURE);
    }

    udpAddress = *(struct sockaddr_in *)(addressInfo->ai_addr);
    freeaddrinfo(addressInfo);
    return udpAddress;
}

int makeSocket(int domain, int type)
{
    int socketFd;
    socketFd = socket(domain, type, 0);
    if(socketFd < 0) 
        ERR("socket");

    return socketFd;
}

void doClient(int clientFd)
{
    char serverName[1024];
    serverName[1023] = '\0';
    gethostname(serverName, 1023);
    struct sockaddr_in serverAddress = makeAddress(serverName, PORT);

    printf("Ready to communicate with %s\n", serverName);
    int bytesSent;
    if ((bytesSent=sendto(clientFd, MSG_LOGIN, strlen(MSG_LOGIN), 0,
         (struct sockaddr *)&serverAddress, sizeof(struct sockaddr))) < 0) 
    {
        ERR("sendto");
    }
    uint structSize = sizeof(struct sockaddr), bytesReceived;
    char message[MAX_BUF];

    if ((bytesReceived = recvfrom(clientFd, message, MAX_BUF-1, 0,
            (struct sockaddr *)&serverAddress, &structSize)) == -1) 
    {
        ERR("recvfrom");
    }

    message[bytesReceived] = '\0';
    printf("Got \"%s\"\n", message);

    sleep(1);

    srand(time(NULL));
    int messageIndex = rand() % 2;
    char* testMessage = MSG_LOGIN;
    if(messageIndex == 1)
        testMessage = MSG_LOGOUT;

    if ((bytesSent=sendto(clientFd, testMessage, strlen(testMessage), 0,
         (struct sockaddr *)&serverAddress, sizeof(struct sockaddr))) < 0) 
    {
        ERR("sendto");
    }

    if ((bytesReceived = recvfrom(clientFd, message, MAX_BUF-1, 0,
            (struct sockaddr *)&serverAddress, &structSize)) == -1) 
    {
        ERR("recvfrom");
    }

    message[bytesReceived] = '\0';
    printf("Got \"%s\"\n", message);
}

int main(int argc, char **argv)
{
    if (argc != 1)
        usage(argv[0]);


    int socketFd = makeSocket(AF_INET, SOCK_DGRAM);
    setSignalHandler(SIGINT, handleSigInt);

    doClient(socketFd);

    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");    
    return EXIT_SUCCESS;
} 