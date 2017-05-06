#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "2000"

#define ERR(source) ( fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                        perror(source), exit(EXIT_FAILURE) )

void usage(char *fileName)
{
    fprintf(stderr, "Usage: %s\n", fileName);
    exit(EXIT_FAILURE);
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

int main(int argc, char **argv)
{
    if (argc != 2)
        usage(argv[0]);

    char serverName[1024];
    serverName[1023] = '\0';
    gethostname(serverName, 1023);

    int socketFd = makeSocket(AF_INET, SOCK_DGRAM);
    
    struct sockaddr_in serverUdpAddress = makeAddress(serverName, PORT);
    int bytesSent;
    if ((bytesSent=sendto(socketFd, argv[1], strlen(argv[1]), 0,
         (struct sockaddr *)&serverUdpAddress, sizeof(struct sockaddr))) < 0) 
    {
			ERR("sendto");
    }

    printf("sent %d bytes to %s\n", bytesSent, serverName);
    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");    
    return EXIT_SUCCESS;
} 