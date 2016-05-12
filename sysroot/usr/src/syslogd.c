#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
int handle_client(int client)
{
	char buffer[1024];
	ssize_t amount = read(client, buffer, 1023);
	if(amount < 0 && errno == EAGAIN) {
		return 0;
	}
	if(amount < 0) {
		perror("read from client");
		return -1;
	} else if(amount == 0) {
		close(client);
		return -1;
	}
	buffer[amount] = 0;
	fprintf(stderr, "%s", buffer);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	fprintf(stderr, "Hello world from syslogd!\n");
	unlink("/dev/log");

	int master = socket(AF_UNIX, SOCK_STREAM, 0);
	printf("Started syslogd with socket %d\n", master);
	struct sockaddr_un addr = {
		AF_UNIX, "/dev/log"
	};

	if(bind(master, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("bind");
		exit(1);
	}

	if(listen(master, 20) == -1) {
		perror("listen");
		exit(1);
	}

	fd_set active_fds, read_fds, err_fds;
	FD_ZERO(&active_fds);
	FD_SET(master, &active_fds);
	while(true) {
		read_fds = active_fds;
		err_fds = active_fds;
		select(FD_SETSIZE, &read_fds, NULL, &err_fds, NULL);
		for(int i=0;i<FD_SETSIZE;i++) {
			if(FD_ISSET(i, &read_fds)) {
				if(i == master) {
					struct sockaddr_un cliaddr;
					socklen_t len;
					int client;
					client = accept(master, (struct sockaddr *)&cliaddr, &len);
					if(client > 0) {
						FD_SET(client, &active_fds);
					} else {
						perror("accept");
					}
					int flags;
					if((flags = fcntl(client, F_GETFL)) < 0) {
						perror("fcntl");
						FD_CLR(client, &active_fds);
					} else {
						if(fcntl(client, F_SETFL, flags | O_NONBLOCK) < 0) {
							perror("fcntl");
							FD_CLR(client, &active_fds);
						}
					}
				} else {
					if(handle_client(i) < 0) {
						close(i);
						FD_CLR(i, &active_fds);
					}
				}
			} else if(FD_ISSET(i, &err_fds)) {
				if(i == master) {
					fprintf(stderr, "syslogd: found error condition on /dev/log.\n");
				} else {
					fprintf(stderr, "closing client %d due to error\n", i);
					close(i);
					FD_CLR(i, &active_fds);
				}
			}
		}

	}

	return 0;
}

