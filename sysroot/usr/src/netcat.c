#include <netinet/in.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

const char *progname;
bool verbose = false;

void usage(void)
{
	fprintf(stderr, "usage (client): %s <host> <port>\n", progname);
	fprintf(stderr, "usage (server): %s -l <port>\n", progname);
}

void server(char *service)
{
	struct addrinfo hint = {
		.ai_family = AF_INET6,
		.ai_flags = AI_PASSIVE,
	};
	struct addrinfo *rai;

	int r;
	if((r=getaddrinfo(NULL, service, &hint, &rai)) < 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
	}

	struct sockaddr_in6 *sa = (struct sockaddr_in6 *)rai->ai_addr;

	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if(sock == -1) {
		perror("socket");
		exit(1);
	}

	int option = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) {
		perror("setsockopt");
		exit(1);
	}

	if(bind(sock, (void *)sa, sizeof(*sa)) == -1) {
		perror("bind");
		exit(1);
	}
	if(listen(sock, 1) == -1) {
		perror("listen");
		exit(1);
	}

	struct sockaddr_in6 cl;
	socklen_t cllen = sizeof(cl);
	int clientsock = accept(sock, (void *)&cl, &cllen);
	if(clientsock < 0) {
		perror("accept");
		exit(1);
	}
	if(verbose) {
		char buf[128];
		fprintf(stderr, "accepting connection from %s\n", inet_ntop(AF_INET6, &cl.sin6_addr, buf, 128));
	}
	char buffer[1024];
	ssize_t len;
	while((len=read(0, buffer, 1024)) > 0) {
		if(send(clientsock, buffer, len, 0) < 0) {
			perror("send");
			exit(1);
		}
	}
}

void client(char *node, char *service)
{
	struct addrinfo hint = {
		.ai_family = AF_INET6,
		.ai_flags = AI_V4MAPPED,
	};
	struct addrinfo *rai;

	int r;
	if((r=getaddrinfo(node, service, &hint, &rai)) < 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
	}

	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if(sock == -1) {
		perror("socket");
		exit(1);
	}

	struct sockaddr_in6 *sa = (struct sockaddr_in6 *)rai->ai_addr;
	if(connect(sock, (void *)sa, sizeof(*sa)) == -1) {
		perror("connect");
		exit(1);
	}

	char buffer[1024];
	ssize_t len;
	while((len = recv(sock, buffer, len, 0)) > 0) {
		if(write(1, buffer, len) == -1) {
			perror("write");
			exit(1);
		}
	}
	if(len < 0) {
		perror("recv");
	}
}

int main(int argc, char **argv)
{
	progname = argv[0];
	int c;
	bool listener = false;
	while((c=getopt(argc, argv, "hlv")) != -1) {
		switch(c) {
			case 'v':
				verbose = true;
				break;
			case 'h':
				usage();
				exit(0);
			case 'l':
				listener = true;
				break;
			default:
				usage();
				exit(1);
		}
	}
	if(listener) {
		if(optind >= argc) {
			usage();
			exit(1);
		}
		char *service = argv[optind];
		server(service);
	} else {
		if(optind + 1 >= argc) {
			usage();
			exit(1);
		}
		char *node = argv[optind];
		char *service = argv[optind + 1];
		client(node, service);
	}

	return 0;
}

