#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 512
#define MAX_CLIENT  128
#define NICKNAME_LEN 32
#define INFTIM ((int)-1)

enum commands
{
	SHOW_LIST = 1,
	CHOOSE_INTERLOCUTOR
};

typedef struct client
{
	uint16_t interlocutor;
	uint16_t port;
	char nickname[NICKNAME_LEN];
	char addr[32];
}client_info;

client_info clients[MAX_CLIENT];
struct pollfd fds[MAX_CLIENT];

const char const instructions[] = 
"1 - Show possible interlocutors\n"
"2 - Choose an interlocutor:\n"
"[*]To create a conversation with one of "
"the users write number 2 and the NAME "
"of the interlocutor separated by SPACE\n"
"(Ex.: \"2 John\")\n";

void dieWithError(const char *msg)
{
	perror(msg);
	exit(-1);
}

void warn(const char *msg)
{
	printf("[-]{%m}ERROR: %s\n", msg);
	fflush(stdout);
}

void show_client_list(const int sock)
{
	char temp[BUFFER_SIZE/4] = {0};
	char list[BUFFER_SIZE] = {0};
	uint8_t count = 0;

	for (uint8_t i = 1; i < MAX_CLIENT; i++)
	{
		if ((fds[i].fd > 0) && (clients[i].interlocutor == 0) && (fds[i].fd != sock))
		{
			count++;
			snprintf(temp, strlen(clients[i].nickname) + 8, "[%d]%s\n", count, clients[i].nickname);
			strncat(list, temp, strlen(temp));
			memset(temp, '\0', sizeof(temp));
		}
	}
	if (!count)
	{
		if (send(sock, "[*]No other users yet\n", 22, 0) < 0)
			warn("send msg");
	}
	else
		if (send(sock, list, strlen(list)-1, 0) < 0)
			warn("send list");
}

void parse_commands(const int index, const char *command)
{
	if (command[0] == '~' && !strlen(clients[index].nickname))
	{
		strncpy(clients[index].nickname, &command[1], strlen(command) - 1);
		printf("newname: %s\n", clients[index].nickname);
		return;
	}

	switch (atoi(&command[0]))
	{
		case SHOW_LIST:
			show_client_list(fds[index].fd);
			break;	
		case CHOOSE_INTERLOCUTOR:
			if (command[1] == ' ')
			{
				for(uint16_t i = 1; i < MAX_CLIENT;i++)
				{		
					if (strlen(clients[i].nickname) != 0)
					{
						if (!strncmp(clients[i].nickname, &command[2], strlen(clients[i].nickname)))
						{
							clients[index].interlocutor = fds[i].fd;
							clients[i].interlocutor = fds[index].fd;
							if (send(fds[index].fd, "[!]A new conversation was created\n", 34, 0) < 0)
								warn("send");  
							if (send(fds[i].fd, "[!]A new conversation was created\n", 34, 0) < 0)
								warn("send");
							printf("[!]A new conversation was created:\n%s with %s\n", clients[i].nickname, clients[index].nickname); 
							return;
						}
					}
				}
			}
			if (send(fds[index].fd, "[-]Unknown user, try again\n", 27, 0) < 0)
				warn("send unknown");
			break;
		default:
			if (send(fds[index].fd, "[-]Unknown command, try again\n", 30, 0) < 0)
				warn("send unknown");
			break;
	}	
}


int main(const int const argc, const char ** const argv)
{
	int16_t clntSock;
	int16_t listenSock;
	int16_t ready;
	int16_t flags;
	uint16_t j;
	uint16_t maxfd;
	int32_t recvSize;
	char buffer[BUFFER_SIZE];
	struct sockaddr_in servAddr;
	struct sockaddr_in clntAddr;

	if (argc != 2)
		dieWithError("Usage: <port>");

	if ((listenSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		dieWithError("socket");

	memset(&clients, 0, sizeof(clients));
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = INADDR_ANY;
	servAddr.sin_port = htons(atoi(argv[1]));

	if (bind(listenSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
		dieWithError("bind");

	if ((flags = fcntl(listenSock, F_GETFL)) < 0)
		dieWithError("fcntl getfl");
	flags |= O_NONBLOCK;
	if (fcntl(listenSock, F_SETFL, flags) < 0)
		dieWithError("fcntl setfl");

	if (listen(listenSock, MAX_CLIENT) < 0)
		dieWithError("listen");

	// initialize 1st element of pollfd's structure array with server socket 
	fds[0].fd = listenSock;
	fds[0].events = POLLIN;	

	// initialize all other elements with -1 (means they are not used by other clients)
	for(uint16_t i = 1; i < MAX_CLIENT; i++)
		fds[i].fd = -1;
	maxfd = 0;

	for (;;)
	{
		for(uint16_t i = 0; i < MAX_CLIENT; i++)
			fds[i].revents = 0;

		// check our sockets for incoming data
		if ((ready = poll(fds, maxfd + 1, INFTIM)) < 0)
			dieWithError("epoll_wait");

		if (fds[0].revents & POLLIN)
		{// if a new connection has arrived on the server socket
			socklen_t clntLen = sizeof(clntAddr);
			if ((clntSock = accept4(listenSock, (struct sockaddr*)&clntAddr, &clntLen, SOCK_NONBLOCK)) < 0)
			{
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				{
					if (--ready <= 0)
						continue;
				}
				else
				{
					warn("accept");
					continue;
				}
			}

			for (j = 1; j < MAX_CLIENT; j++)
			{// look for an empty spot
				if (fds[j].fd < 0)
				{
					fds[j].fd = clntSock;
					fds[j].events = POLLIN;

					if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clients[j].addr, 32) < 0)
						warn("inet_ntop");
					if ((clients[j].port = ntohs(clntAddr.sin_port)) < 0)
						warn("ntohs");
						
					if (send(fds[j].fd, instructions, strlen(instructions), 0) < 0)
						warn("send");

					printf("[!]New connection on socket: %d from IP: %s port: %d\n", 
												fds[j].fd, clients[j].addr, clients[j].port);
					break;
				}
			}

			if (j == MAX_CLIENT)
				printf("Too many clients\n");
			if (j > maxfd)
				maxfd = j;

			// if there is no data recieved on client sockets
			if (--ready <= 0)
				continue;
		}			

		for (int16_t i = 1; i < maxfd + 1; i++)
		{// if there is some information received for one or more client socket
			if (fds[i].fd < 0)
				continue;

			memset(buffer, '\0', BUFFER_SIZE);
			if (fds[i].revents & POLLIN)
			{
				if ((recvSize = recv(fds[i].fd , buffer, BUFFER_SIZE, 0)) < 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						continue;
					else
					{
						warn("recv");
						shutdown(fds[i].fd, 2);
						close(fds[i].fd);
						fds[i].fd = -1;	
						continue;
					}
				}
				if (recvSize == 0)
				{
					printf("[*]Client closed the connection\n");

					/* if user closed the connection while he had a coversation, 
					 * tell his interlocutor that the conversation is over
					 */
					if (clients[i].interlocutor)
					{
						for (int16_t j = 1; j < MAX_CLIENT; j++)
						{	
							if (clients[i].interlocutor == fds[j].fd)
							{
								char convEndMsg[] = "[*]Your conversation partner left the chat\n";
								strncat(convEndMsg, instructions, strlen(instructions));
								if (send(fds[j].fd, convEndMsg, 43 + strlen(instructions), 0) < 0)
									warn("send");
								clients[j].interlocutor = 0;
							}
						}
					}
					memset(&clients[i], 0, sizeof(struct client));
					
					shutdown(fds[i].fd, 2);
					close(fds[i].fd);
					fds[i].fd = -1;	
					break;
				}

				printf("[%d]buffer: %s\n",fds[i].fd, buffer);

				// if user doesn't have any interlocutor means he's sending commands to server
				if (!clients[i].interlocutor)
					parse_commands(i, buffer);
				else
					if (send(clients[i].interlocutor, buffer, strlen(buffer), 0) < 0)
						warn("send message");

				if (--ready <= 0)
					break;
			}
		}
	}

	return 0;
}