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
#include <sys/mman.h>
#include <pty.h>

void handler(int sig)
{
	fprintf(stderr, "SIGNAL HANDLER! %d\n", sig);
}

int main(int argc, char **argv)
{
/*
	int f = open("/dev/vga", O_RDWR);
	
	void *addr = mmap(NULL, 0x1000, PROT_WRITE | PROT_READ, MAP_SHARED, f, 0);
	printf("%p\n", addr);
	unsigned char *screen = addr;

	*screen = 'H';
	*(screen+1) = 0x07;

	printf("done\n");

	int master, slave;

	char name[128];
	int r = openpty(&master, &slave, name, NULL, NULL);
	fprintf(stderr, ":: %d %d %d : %s\n", r, master, slave, name);

	write(master, "Hello wm\n", 9);
	write(slave, "Hello ws\n", 9);

	fprintf(stderr, "okay\n");

	char buf[128];
	r = read(master, buf, 128);
	fprintf(stderr, "m: %d : %s\n", r, buf);
	r = read(slave, buf, 128);
	fprintf(stderr, "s: %d : %s\n", r, buf);

	for(;;);
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
		close(2);
		close(1);
		close(0);
		open("/dev/com0", O_RDWR);
		open("/dev/com0", O_RDWR);
		open("/dev/com0", O_RDWR);
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

	if(!fork()) {
		execlp("cond", "cond",
				//"-a", "login",
				//"-1", "sh /etc/rc/boot",
				"-1", "test", (char *)NULL);
		execvp("/cond", NULL);
		perror("execvp cond");
		exit(1);
	}

	for(;;) sleep(100);

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

