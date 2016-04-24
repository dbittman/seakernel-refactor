#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main()
{
    sigset_t set;
    int status;

    if (getpid() != 1) return 1;

    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, 0);

    if (fork()) for (;;) wait(&status);

    sigprocmask(SIG_UNBLOCK, &set, 0);

    setsid();
    setpgid(0, 0);

	switch(fork()) {
		int logfd;
		case 0:
			logfd = open("/dev/com0", O_RDWR);
			if(logfd == -1)
				exit(1);
			dup2(logfd, 0);
			dup2(logfd, 1);
			dup2(logfd, 2);
			execl("/bin/syslogd", "syslogd", (char *)NULL);
			exit(1);
		case -1:
			return 1;
	}

	int fd;
	while((fd = open("/dev/log", O_WRONLY)) == -1)
		;
	close(fd);

	openlog("init", 0, LOG_DAEMON);

	syslog(LOG_NOTICE, "mounting root");

	if(mount("/dev/ada0", "/mnt", NULL, 0, NULL) == -1) {
		syslog(LOG_EMERG, "failed to mount root device: %s", strerror(errno));
		exit(1);
	}
	mkdir("/mnt/dev", 0777);
	if(mount("/dev", "/mnt/dev", NULL, MS_BIND, NULL) == -1) {
		syslog(LOG_EMERG, "failed to remount devfs: %s", strerror(errno));
		exit(1);
	}

	syslog(LOG_NOTICE, "chrooting");
	if(chdir("/mnt") == -1) {
		syslog(LOG_EMERG, "failed to chdir: %s", strerror(errno));
		exit(1);
	}
	if(chroot("/mnt") == -1) {
		syslog(LOG_EMERG, "failed to chroot: %s", strerror(errno));
		exit(1);
	}
	return execl("/bin/cond", "cond", "-a", "bash --login", "-1", "bash --login", (char *)NULL);
}

