#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sys/socket.h>
int main(int argc, char **argv)
{
	FILE *out = fopen("/com0", "w");
	
	fprintf(out, "hello world!\n");
/*
	void openlog(const char *ident, int option, int facility);
	void syslog(int priority, const char *format, ...);
	void closelog(void);
*/

	int sv[2];
	int r = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	fprintf(out, "GOT %d %d\n", sv[0], sv[1]);

	write(sv[0], "test", 4);
	char b[4];
	read(sv[1], b, 4);
	fprintf(out, ":: %s\n", b);

	/*
	openlog("init", 0, LOG_USER);
	syslog(LOG_WARNING, "Test log message\n");
	closelog();
	*/
	return 0;
}

