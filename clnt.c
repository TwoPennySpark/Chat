#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>

#define BUFFER_SIZE 512
#define MAX_CLIENT  128
#define MAX_NAME_SIZE 32

void dieWithError(char *msg)
{
	perror(msg);
	exit(-1);
}

void warn(char *msg)
{
	printf("[-]{%m}ERROR: %s\n", msg);
	fflush(stdout);
}

void *chatRead(void *arg)
{
	int16_t sock = *((int*)arg);
	int32_t recvSize;
	time_t rawtime;
	struct tm* timeinfo;
	char buffer[BUFFER_SIZE];
	char timebuf[BUFFER_SIZE];

	for (;;)
	{
		memset(&buffer, '\0', BUFFER_SIZE);
		if ((recvSize = recv(sock, buffer, BUFFER_SIZE, 0)) < 0)
		{
			if (errno == ECONNRESET)
			{
				printf("Connection is reset by client\n");
				shutdown(sock, 2);
				close(sock);
				break;
			}
			else
				dieWithError("chatRead recv() failed");
		}
		else if (recvSize == 0)
		{
			printf("Client closed connection\n");
			shutdown(sock, 2);
			close(sock);
			break;
		}
		else
		{
			time(&rawtime);
			timeinfo = localtime(&rawtime);
			sprintf(timebuf, "%d:%d", timeinfo->tm_hour, timeinfo->tm_min);
			printf("*********************\n\t%s\n---------------------\n"
							"%s\n*********************\n", timebuf, buffer);
		}
	} 
	pthread_exit(0);
}

void *chatWrite(void *arg)
{
	uint16_t sock = *((int*)arg);
	char buffer[BUFFER_SIZE];

	memset(&buffer, '\0', BUFFER_SIZE);
	for (;;)
	{
		if (fgets(buffer, BUFFER_SIZE, stdin) < 0)
			dieWithError("chatWrite fgets() failed"); 
		if (send(sock, buffer, strlen(buffer), 0) < 0)
				dieWithError("chatWrite send() failed");
		memset(&buffer, '\0', BUFFER_SIZE);
	}
	pthread_exit(0);
}

int main(int argc, char** argv)
{
	int16_t size = 0;
	int16_t sock;
	char buffer[BUFFER_SIZE];
	char nickname[MAX_NAME_SIZE] = "~";
	struct sockaddr_in servAddr;
	pthread_t readPth, writePth;

	while (size < 4 || size > MAX_NAME_SIZE - 1)
	{
		printf("[*]Enter your nickname (it must consist of at least 3"
							 "characters and be no longer than 31 characters):\n");
		memset(&nickname[1], '\0', MAX_NAME_SIZE - 1);
		if ((size = read(1, &nickname[1], BUFFER_SIZE)) < 0)
			dieWithError("read");
	}
	printf("[*]Your nickname is: %s\n", &nickname[1]);

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		dieWithError("scoket");

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	if (inet_pton(AF_INET, argv[1], &servAddr.sin_addr.s_addr) < 0)
		dieWithError("inet_pton");
	servAddr.sin_port = htons(atoi(argv[2]));

	if (connect(sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
		dieWithError("connect");

	if (send(sock, nickname, strlen(nickname), 0) < 0)
		dieWithError("send");

	memset(&buffer, '\0', BUFFER_SIZE);
	if (recv(sock, buffer, BUFFER_SIZE, 0) < 0)
		dieWithError("recv");

	printf("%s\n", buffer);
	if (pthread_create(&writePth, NULL, &chatWrite, &sock) < 0)
		dieWithError("pthread_create() failed");
	if (pthread_create(&readPth, NULL, &chatRead, &sock) < 0)
		dieWithError("pthread_create() failed"); 
	if (pthread_join(writePth, NULL) < 0)
		dieWithError("join() failed");	
	if (pthread_join(readPth, NULL) < 0)
		dieWithError("join() failed");

	return 0;
}