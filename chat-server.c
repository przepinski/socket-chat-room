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
#include <arpa/inet.h>

#define PORT "2000"
#define MAX_BUF 200
#define MAX_CLIENTS 2

#define ERR(source) ( fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                        perror(source), exit(EXIT_FAILURE) )

#define MSG_LOGIN "#login"
#define MSG_LOGOUT "#logout"

#define MSG_PING "#ping"
#define MSG_PONG "#pong"
#define MSG_CLOSED "#closed"

volatile sig_atomic_t clientsCount = 0;

typedef struct clientArgs
{
    int id;
    struct sockaddr_in address;
    struct timeval lastPong;
} clientArgs_t;

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
volatile sig_atomic_t lastSignal = 0;
void handleSigInt(int signal)
{
    shouldQuit = 1;
    lastSignal = signal;
}

void handleSigAlrm(int signal)
{
    lastSignal = signal;
}

int makeSocket(int domain, int type)
{
    int socketFd;
    socketFd = socket(domain, type, 0);
    if(socketFd < 0) 
        ERR("socket");

    return socketFd;
}

int bindUdpSocket(uint16_t port)
{
    struct sockaddr_in udpAddress; 
    int yes = 1;

    int socketFd = makeSocket(AF_INET, SOCK_DGRAM);
    memset(&udpAddress, 0, sizeof(struct sockaddr_in));
    udpAddress.sin_family = AF_INET;
    udpAddress.sin_port = htons(port);
    udpAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) 
        ERR("setsockopt");

    if(bind(socketFd, (struct sockaddr*)&udpAddress, sizeof(udpAddress)) < 0)  
        ERR("bind");

    return socketFd;
}

void sendMessage(int serverFd, clientArgs_t client, char* message)
{
    int bytesSent;
    printf("[SENT] \"%s\" to %d\n\n", message, client.id);
    // printf("Family: %d, should be: %d\n", client.address.sin_family, AF_INET);
    // printf("IP: %lu, Port: %d\n", (unsigned long)client.address.sin_addr.s_addr, client.address.sin_port);
    if ((bytesSent=sendto(serverFd, message, strlen(message), 0,
         (struct sockaddr *)&client.address, sizeof(struct sockaddr))) < 0) 
    {
        ERR("sendto");
    }
}

void sendMessageToOther(int serverFd, clientArgs_t clients[], int senderId, char* message)
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id != -1 && clients[i].id != senderId)
        {
            sendMessage(serverFd, clients[i], message);
        }
    }
}

void logoutClient(int serverFd, clientArgs_t clients[], int clientId)
{
    char tmpMessage[MAX_BUF];
    sendMessage(serverFd, clients[clientId], MSG_LOGOUT);

    clients[clientId].id = -1;
    clientsCount--;

    snprintf(tmpMessage, MAX_BUF, "user %d logged out", clientId);
    sendMessageToOther(serverFd, clients, clientId, tmpMessage);
}

void receiveMessage(int serverFd, clientArgs_t clients[], clientArgs_t* tmpClient, char* message)
{
    uint structSize = sizeof(struct sockaddr), bytesReceived;
    tmpClient->id = -1;

    if ((bytesReceived = recvfrom(serverFd, message, MAX_BUF-1, 0,
            (struct sockaddr *)&tmpClient->address, &structSize)) == -1) 
    {
        if (errno == EINTR)
        {
            tmpClient->id = -2;
            return;
        }
            
        ERR("recvfrom");
    }
    message[bytesReceived] = '\0';

    //printf("[REQUEST]Client on port: %hu\n", clientAddress.sin_port);
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id != -1 && clients[i].address.sin_port == tmpClient->address.sin_port)
        {
            *tmpClient = clients[i];
            break;
        }
    }

    printf("[GOT] \"%s\" from %d\n", message, tmpClient->id);
}

void checkKeepAlive(int serverFd, clientArgs_t clients[])
{
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    long secondsSinceLastPong;

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id != -1)
        {
            secondsSinceLastPong  = currentTime.tv_sec  - clients[i].lastPong.tv_sec;
            if (secondsSinceLastPong >= 6)
            {
                printf("\tNo response from %d - logging out\n", i);
                logoutClient(serverFd, clients, i);
            }
        }
    }
}

void acceptNewClient(int serverFd, clientArgs_t* currentClient, clientArgs_t clients[])
{
    char tmpMessage[MAX_BUF];
    int freeId = -1;
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].id == -1)
        {
            freeId = i;
            break;
        }
    }
    snprintf(tmpMessage, MAX_BUF, "new user %d", freeId);
    sendMessageToOther(serverFd, clients, freeId, tmpMessage);

    clients[freeId].id = freeId;
    clients[freeId].address = currentClient->address;
    gettimeofday(&clients[freeId].lastPong, NULL);
    clientsCount++;

    snprintf(tmpMessage, MAX_BUF, "#ack %d", freeId);
    sendMessage(serverFd, clients[freeId], tmpMessage);
}

void doServer(int serverFd)
{
    clientArgs_t clients[MAX_CLIENTS];
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].id = -1;
    }

    char message[MAX_BUF];

    alarm(3);
    while (!shouldQuit)
    {
        memset(message, 0x00, MAX_BUF);
        checkKeepAlive(serverFd, clients);

        if(lastSignal == SIGALRM)
        {
            sendMessageToOther(serverFd, clients, -1, MSG_PING);
            lastSignal = -1;
            alarm(3);
        }

        clientArgs_t currentClient;
        receiveMessage(serverFd, clients, &currentClient, message);
        int clientId = currentClient.id;
        if(clientId == -2)
            continue;

        // Detect message type
        char tmpMessage[MAX_BUF];
        if(strcmp(message, MSG_LOGIN) == 0)
        {
            if(clientId != -1) // DENY
            {
                logoutClient(serverFd, clients, clientId);
            }
            else if(clientsCount >= MAX_CLIENTS) // DENY
            {
                sendMessage(serverFd, currentClient, MSG_LOGOUT);
            }
            else if(clientsCount < MAX_CLIENTS) // ACCEPT
            {
                acceptNewClient(serverFd, &currentClient, clients);
            }
        }
        else if (strcmp(message, MSG_LOGOUT) == 0)
        {
            logoutClient(serverFd, clients, clientId);
        }
        else if(strcmp(message, MSG_PONG) == 0)
        {
            struct timeval pongTime;
            gettimeofday(&pongTime, NULL);
            clients[clientId].lastPong = pongTime;
        }
        else // message from stdin
        {
            snprintf(tmpMessage, MAX_BUF, "%d: %s", clientId, message);
            sendMessageToOther(serverFd, clients, clientId, tmpMessage);
        }
    }

    // Closing
    sendMessageToOther(serverFd, clients, -1, MSG_CLOSED);
}

int main(int argc, char **argv)
{
    if (argc != 1)
        usage(argv[0]);

    int socketFd = bindUdpSocket(atoi(PORT));

    setSignalHandler(SIGINT, handleSigInt);
    setSignalHandler(SIGALRM, handleSigAlrm);

    printf("Listening for connections on %s\n", PORT);

    doServer(socketFd);

    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");
    return EXIT_SUCCESS;
} 