all:
	gcc serv.c -o serv -Wall --std=c99
	gcc clnt.c -o clnt -Wall --std=c99 -pthread
