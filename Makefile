CC=gcc
CFLAGS= -std=gnu99 -Wall
LDLIBS= -pthread

.PHONY: clean

all: server client

clean:
	rm chat-server chat-client

server: chat-server.c
	$(CC) $(CFLAGS) $(LDLIBS) chat-server.c -o chat-server

client: chat-client.c
	$(CC) $(CFLAGS) $(LDLIBS) chat-client.c -o chat-client