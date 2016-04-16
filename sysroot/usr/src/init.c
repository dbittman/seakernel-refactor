#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sys/socket.h>
#include <sys/un.h>

void server(void)
{
	fprintf(stderr, "Hello from server!\n");

	int s = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = {
		AF_UNIX, "/test_sock" };

	if(bind(s, &addr, sizeof(addr)) == -1)
		perror("bind");

	listen(s, 20);

	struct sockaddr_un _q;
	socklen_t l;
	int a;
	while((a = accept(s, &_q, &l)) != -1) {
		char buf[128];
		memset(buf, 0, sizeof(buf));
		int r;
		while((r = read(a, buf, 128)) > 0)
			fprintf(stderr, "got data %d: %s\n", r, buf);
		
		fprintf(stderr, "Done, :: %d: %s\n", r, strerror(errno));
		close(a);
	}
	perror("accept");

	for(;;);
}

int main(int argc, char **argv)
{

	fprintf(stderr, "Hello world from init!\n");
	if(!fork()) {
		execvp("/syslogd", NULL);
		perror("execvp");
		exit(1);
	}

	/*

	for(;;);
	if(!fork()) {
		fprintf(stderr, "Hello from client!\n");

		int s = socket(AF_UNIX, SOCK_STREAM, 0);
		fprintf(stderr, "GOT SOCKET %d\n", s);
		struct sockaddr_un addr = {
			AF_UNIX,
			"/test_sock"};

		if(connect(s, &addr, sizeof(addr)) == -1)
			perror("connect");

		write(s, "Hello, server!\n", 15);

		exit(0);
	}

	server();
	*/
/*
	void openlog(const char *ident, int option, int facility);
	void syslog(int priority, const char *format, ...);
	void closelog(void);
*/
/*
	int sv[2];
	int r = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	fprintf(out, "GOT %d %d\n", sv[0], sv[1]);

	write(sv[0], "test", 4);
	char b[4];
	read(sv[1], b, 4);
	fprintf(out, ":: %s\n", b);
*/
	for(volatile int i=0;i<1000000l;i++) {

	}
	fprintf(stderr, "START LOG\n");
	if(!fork())  {
		openlog("init2", 0, LOG_USER);
		while(1) {
			syslog(LOG_WARNING, "Test log message\n");
			int r = open("/dev/null", O_RDONLY);
			char buf[1];
			read(r, buf, 0);
			for(volatile int i=0;i<10000l;i++);
		}
		closelog();
	}
	openlog("init", 0, LOG_USER);
	while(1) {
		syslog(LOG_WARNING, "Test log message\n");
			int r = open("/dev/null", O_RDONLY);
			char buf[1];
			read(r, buf, 0);
			for(volatile int i=0;i<10000l;i++);
	}
	closelog();
	return 0;
}

