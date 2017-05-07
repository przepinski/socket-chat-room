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
void handleSigInt(int signal)
{
    shouldQuit = 1;
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
    if ((bytesSent=sendto(serverFd, message, strlen(message), 0,
         (struct sockaddr *)&client.address, sizeof(struct sockaddr))) < 0) 
    {
			ERR("sendto");
    }
}

void sendMessageToAll(int serverFd, clientArgs_t clients[], char* message)
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id != -1)
        {
            sendMessage(serverFd, clients[i], message);
        }
    }
}

void doServer(int serverFd)
{
    clientArgs_t clients[MAX_CLIENTS];
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].id = -1;
    }

    while (!shouldQuit)
    {
        struct sockaddr_in clientAddress;
        memset(&clientAddress, 0x00, sizeof(struct sockaddr_in));
        uint structSize = sizeof(struct sockaddr), bytesReceived;
        char message[MAX_BUF];

        if ((bytesReceived = recvfrom(serverFd, message, MAX_BUF-1, 0,
                (struct sockaddr *)&clientAddress, &structSize)) == -1) 
        {
			if (errno == EINTR)
			{
				continue;
			}
			ERR("recvfrom");
        }

        // Find sender in clients list
        int clientId = -1;
        //printf("[REQUEST]Client on port: %hu\n", clientAddress.sin_port);
        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(clients[i].id != -1 && clients[i].address.sin_port == clientAddress.sin_port)
            {
                clientId = i;
                break;
            }
        }

        message[bytesReceived] = '\0';
        printf("[GOT] \"%s\" from %d\n", message, clientId);

        // Detect message type
        char tmpMessage[MAX_BUF];
        if(strcmp(message, MSG_LOGIN) == 0) // MSG_LOGIN
        {
            if(clientsCount >= MAX_CLIENTS || clientId != -1)
            {
                clientArgs_t tmpClient;
                tmpClient.address = clientAddress;
                tmpClient.id = -1;
                sendMessage(serverFd, tmpClient, MSG_LOGOUT);
                if(clientId != -1)
                {
                    clients[clientId].id = -1;
                    clientsCount--;
                }
            }
            else if(clientsCount < MAX_CLIENTS)
            {
                // Add client
                for(int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (clients[i].id == -1)
                    {
                        clientId = i;
                        break;
                    }
                }
                snprintf(tmpMessage, MAX_BUF, "new user %d", clientId);
                sendMessageToAll(serverFd, clients, tmpMessage);

                clients[clientId].id = clientId;
                clients[clientId].address = clientAddress;
                clientsCount++;

                snprintf(tmpMessage, MAX_BUF, "#ack %d", clientId);
                sendMessage(serverFd, clients[clientId], tmpMessage);
            }
        }
        else if (strcmp(message, MSG_LOGOUT) == 0) // MSG_LOGOUT
        {
            sendMessage(serverFd, clients[clientId], MSG_LOGOUT);

            clients[clientId].id = -1;
            clientsCount--;

            snprintf(tmpMessage, MAX_BUF, "user %d logged out", clientId);
            sendMessageToAll(serverFd, clients, tmpMessage);
        }
        else // message from stdin
        {

        }
    }

    // Closing
    sendMessageToAll(serverFd, clients, MSG_CLOSED);
}

int main(int argc, char **argv)
{
    if (argc != 1)
        usage(argv[0]);

    int socketFd = bindUdpSocket(atoi(PORT));

    setSignalHandler(SIGINT, handleSigInt);

    printf("Listening for connections on %s\n", PORT);

    doServer(socketFd);

    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");
    return EXIT_SUCCESS;
} 