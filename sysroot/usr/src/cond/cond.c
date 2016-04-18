#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <pty.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <stdio.h>
#include <signal.h>

#include "cond.h"

char *command[9];
char *autospawn = NULL;

struct termios def_term = {
	.c_iflag = BRKINT | ICRNL,
	.c_oflag = ONLCR,
	.c_lflag = ICANON | ECHO | ISIG,
	.c_cc[VEOF] = 4,
	.c_cc[VEOL] = 0,
	.c_cc[VERASE] = '\b',
	.c_cc[VINTR] = 3,
	.c_cc[VMIN] = 1,
	.c_cc[VSUSP] = 26,
};

struct winsize def_win = {
	.ws_row = 25,
	.ws_col = 80
};

struct pty *ptys[MAX_TERMS];
struct pty *current_pty = NULL;
int keyfd;

struct pty *create_pty(struct termios *term, struct winsize *win)
{
	assert(term);
	assert(win);
	struct pty *p = calloc(1, sizeof(struct pty));
	memcpy(&p->term, term, sizeof(*term));
	memcpy(&p->win, win, sizeof(*win));
	p->cattr = DEF_ATTR;
	p->sp = LINES-25;
	p->cy = LINES-25;
	return p;
}

bool pty_is_icanon(struct pty *pty)
{
	struct termios term;
	tcgetattr(pty->masterfd, &term);
	return (term.c_lflag & ICANON) != 0;
}

struct pty *spawn_terminal(char *cmd)
{
	syslog(LOG_INFO, "spawning new terminal: %s\n", cmd);
	struct pty *p = create_pty(&def_term, &def_win);
	clear(p);
	int pid = forkpty(&p->masterfd, NULL, &p->term, &p->win);
	if(!pid) {
		execlp(cmd, cmd, (char *)NULL);
		//execlp("sh", "sh", "-c", cmd, (char *)NULL);
		exit(0);
	}
	int flags = fcntl(p->masterfd, F_GETFL, 0);
	if(flags >= 0)
		flags |= O_NONBLOCK;
	fcntl(p->masterfd, F_SETFL, flags);
	return p;
}

void switch_console(int con)
{
	struct pty *n;
	if(!ptys[con]) {
		if(autospawn) {
			n = spawn_terminal(autospawn);
			ptys[con] = n;
		} else {
			return;
		}
	} else {
		n = ptys[con];
	}
	current_pty = n;
	flip(n);
	update_cursor(n);
}

void select_loop(void)
{
	while(true) {
		fd_set readers;
		FD_ZERO(&readers);
		int max = 0;
		for(int i=0;i<MAX_TERMS;i++) {
			if(ptys[i]) {
				FD_SET(ptys[i]->masterfd, &readers);
				if(ptys[i]->masterfd > max)
					max = ptys[i]->masterfd;
			}
		}
		FD_SET(keyfd, &readers);
		if(keyfd > max)
			max = keyfd;
		int r = select(max+1, &readers, NULL, NULL, NULL);
		if(r > 0) {
			for(int i=0;i<MAX_TERMS;i++) {
				if(ptys[i] && FD_ISSET(ptys[i]->masterfd, &readers)) {
					/* data available. */
					process_output(ptys[i]);
				}
			}
			if(FD_ISSET(keyfd, &readers)) {
				/* keyboard data available */
				read_keyboard();
			}
		}

	}
}

void help()
{
	fprintf(stderr, "cond - Console Daemon\n");
	fprintf(stderr, "usage: cond [-h] -- command args...\n");
	fprintf(stderr, "'command args' is the command that is to be spawned on"
			"ptys when first accessed.\n");
}

void parse_options(int argc, char **argv)
{
	int c;
	while((c = getopt(argc, argv, "ha:1:2:3:4:5:6:7:8:9:")) != -1) {
		switch(c) {
			case 'h':
				help();
				exit(0);
			case 'a':
				autospawn = optarg;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				command[c - '1'] = optarg;
				break;
		}
	}
}
#include <sys/resource.h>
int main(int argc, char **argv)
{
	//daemon(0, 0);
	setpriority(PRIO_PROCESS, 0, -20);
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	memset(ptys, 0, sizeof(ptys));
	memset(command, 0, sizeof(command));
	openlog("cond", 0, 0);
	parse_options(argc, argv);
	syslog(LOG_INFO, "starting cond v. 0.1\n");
	keyfd = open("/dev/keyboard", O_RDWR | O_NONBLOCK);
	if(keyfd == -1) {
		syslog(LOG_ERR, "failed to open keyboard file: %s\n", strerror(errno));
		exit(1);
	}
	init_screen();
	for(int i=0;i<9;i++) {
		if(command[i]) {
			struct pty *p = spawn_terminal(command[i]);
			ptys[i] = p;
		}
	}
	current_pty = ptys[0];
	flip(current_pty);
	select_loop();
}

