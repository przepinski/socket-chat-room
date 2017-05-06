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

int main(int argc, char **argv)
{
    if (argc != 1)
        usage(argv[0]);

    int socketFd = bindUdpSocket(atoi(PORT));

    setSignalHandler(SIGINT, handleSigInt);

    printf("Listening for connections on %s\n", PORT);
    while (!shouldQuit)
    {
        struct sockaddr_in clientAddress;
        uint structSize, nbytes;
        char buf[MAX_BUF];

        structSize = sizeof(struct sockaddr);
        if ((nbytes=recvfrom(socketFd,buf, MAX_BUF-1, 0,
                        (struct sockaddr *)&clientAddress, &structSize)) == -1) 
        {
			if (errno == EINTR)
			{
				continue;
			}
			ERR("recvfrom");
        }

        printf("Got packet - %d bytes long\n", nbytes);
        buf[nbytes] = '\0';
        printf("Packet contains \"%s\"\n", buf);
    }

    if (close(socketFd) < 0)
        ERR("close");

    printf("Exiting\n");
    return EXIT_SUCCESS;
} 