#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	fprintf(stderr, "Hello world from syslogd!\n");
	unlink("/dev/log");

	int master = socket(AF_UNIX, SOCK_STREAM, 0);
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

	while(true) {
		struct sockaddr_un cliaddr;
		socklen_t len;
		int client = accept(master, (struct sockaddr *)&cliaddr, &len);
		if(client == -1) {
			perror("accept");
			exit(1);
		}

		char buf[1024];
		int amount;
		while((amount=read(client, buf, 1023)) > 0) {
			buf[amount] = 0;
			fprintf(stderr, "%s", buf);
			memset(buf, 0, 1024);
		}
		close(client);
	}

	return 0;
}

