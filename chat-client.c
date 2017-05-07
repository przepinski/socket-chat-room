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
#define STDIN 0

#define ERR(source) ( fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                        perror(source), exit(EXIT_FAILURE) )

#define MSG_LOGIN "#login"
#define MSG_LOGOUT "#logout"

#define MSG_PING "#ping"
#define MSG_PONG "#pong"
#define MSG_CLOSED "#closed"

void usage(char *fidataLengthame)
{
    fprintf(stderr, "Usage: %s\n", fidataLengthame);
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

void sendMessage(int clientFd, struct sockaddr_in serverAddress, char* message)
{
    int bytesSent;
    printf("[SENT] \"%s\" to server\n", message);
    if ((bytesSent=sendto(clientFd, message, strlen(message), 0,
         (struct sockaddr *)&serverAddress, sizeof(struct sockaddr))) < 0) 
    {
			ERR("sendto");
    }
}

void receiveMessage(int clientFd, char* message)
{
    uint bytesReceived;

    if ((bytesReceived = recvfrom(clientFd, message, MAX_BUF-1, 0, NULL, NULL)) == -1) 
    {
        if (errno != EINTR && errno != EAGAIN) // signal or timeout
        {
            ERR("recvfrom");
        }
    }
    message[bytesReceived] = '\0';
    printf("[GOT] \"%s\" from server \n", message);
}

void doClient(int clientFd)
{
    char serverName[1024];
    serverName[1023] = '\0';
    gethostname(serverName, 1023);
    struct sockaddr_in serverAddress = makeAddress(serverName, PORT);

    printf("Ready to communicate with %s\n", serverName);
    char message[MAX_BUF];

    sendMessage(clientFd, serverAddress, MSG_LOGIN);
    receiveMessage(clientFd, message);

    fd_set masterFdsSet, readFdsSet;
    int dataLength;
    char stdinData[MAX_BUF] = {0};

    FD_ZERO(&masterFdsSet);
    FD_SET(STDIN, &masterFdsSet);
    FD_SET(clientFd, &masterFdsSet);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    while(!shouldQuit)
    {
        readFdsSet = masterFdsSet;
        if (pselect(clientFd+1, &readFdsSet, NULL, NULL, NULL, &oldmask) == -1)
        {
            if (errno == EINTR) 
                continue;

            ERR("pselect");
        }

        if (FD_ISSET(STDIN, &readFdsSet))
        {
            if (fgets(stdinData, sizeof(stdinData), stdin) == NULL)
            {
                shouldQuit = 1;
                continue;
            }

            dataLength = strlen(stdinData) - 1;
            if (stdinData[dataLength] == '\n')
                stdinData[dataLength] = '\0';

            printf("Read: %s\n", stdinData);
            sendMessage(clientFd, serverAddress, stdinData);
        }
        if (FD_ISSET(clientFd, &readFdsSet))
        {
            receiveMessage(clientFd, message);
            if(strcmp(message, MSG_LOGOUT) == 0 || strcmp(message, MSG_CLOSED) == 0)
            {
                shouldQuit = 1;
            }
            else 
            {

            }
        }
    }

    sendMessage(clientFd, serverAddress, MSG_LOGOUT);
    receiveMessage(clientFd, message);
    // EXIT
}

int main(int argc, char **argv)
{
    if (argc != 1)
        usage(argv[0]);


    int socketFd = makeSocket(AF_INET, SOCK_DGRAM);
    setSignalHandler(SIGINT, handleSigInt);

    struct timeval messageTimeout;
    messageTimeout.tv_sec = 2;
    messageTimeout.tv_usec = 0;
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &messageTimeout, sizeof(messageTimeout)) < 0) 
    {
        ERR("setsockopt");
    }

    doClient(socketFd);

    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");    
    return EXIT_SUCCESS;
} 