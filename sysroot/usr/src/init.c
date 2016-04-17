#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <signal.h>

void handler(int sig)
{
	fprintf(stderr, "SIGNAL HANDLER! %d\n", sig);
}

int main(int argc, char **argv)
{
	//signal(SIGALRM, handler);
	alarm(1);
	for(;;);
	fprintf(stderr, "Hello world from init!\n");

	/*while(1) {
		fprintf(stderr, "ONE SECOND\n");
		sleep(1);
	}

	for(;;);*/
	if(!fork()) {
		execvp("/syslogd", NULL);
		perror("execvp");
		exit(1);
	}

	while(1) {
		int f = open("/dev/log", O_RDONLY);
		if(f >= 0) {
			close(f);
			break;
		}
	}
	fprintf(stderr, "START LOG\n");
	int pid;
	if(!(pid=fork()))  {
		signal(SIGINT, handler);
		sleep(1);
		openlog("init2", 0, LOG_USER);
		while(1) {
		//fprintf(stderr, "init2\n");
			syslog(LOG_WARNING, "Test log message\n");
			sleep(3);
		}
		closelog();
	}
	fprintf(stderr, "child process pid = %d\n", pid);
	openlog("init", 0, LOG_USER);
	while(1) {
		syslog(LOG_WARNING, "Test log message\n");
			sleep(1);
		fprintf(stderr, "init: SENDING SIGNAL\n");
			kill(pid, SIGINT);
	}
	closelog();
	return 0;
}

