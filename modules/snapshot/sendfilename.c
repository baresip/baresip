#include  <sys/ioctl.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include "sendfilename.h"




int socket_connect(void){
    int sockfd, portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int optval;
    int buffsize;
    int flag;
    int tos_local;

    portno = 8888;

//    sockfd = socket(AF_INET, SOCK_STREAM, 0);

//    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

//    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return -1;
    }



    server = gethostbyname("localhost");
    if (server == NULL) {
    	printf("%s","failed to get hostname face recognizer\n");
        return -1;
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
//    bcopy((char *)server->h_addr_list[0],
//         (char *) &serv_addr.sin_addr.s_addr,
//         server->h_length);

//    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

//    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
//    memset(&serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
    	printf("%s","failed to connect to face recognizer\n");
        return -1;
    }

    tos_local = 0x16;
    setsockopt(sockfd, SOL_IP, IP_TOS,  &tos_local, sizeof(tos_local));

    optval = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(int)) < 0) {
        close(sockfd);
    	printf("%s","failed to set socket options to face recognizer\n");
        return -1;
    }

    buffsize = 1024*1024; // 1 MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(int)) <0) {
        close(sockfd);
    	printf("%s","failed to set socket options part 2 to face recognizer\n");
        return -1;
    }

	flag = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));

	/*
    int so_oobinline;
    so_oobinline = 1;

    if (setsockopt(sockfd, SOL_SOCKET, SO_OOBINLINE, &so_oobinline, sizeof(int)) <0) {
        close(sockfd);
    	printf("%s","failed to set socket options part 2 to face recognizer\n");
        return -1;
    }
*/

	ioctl(3, FIONBIO, &flag);

	printf("%s","connected to face recognizer!\n");

    return sockfd;

}
