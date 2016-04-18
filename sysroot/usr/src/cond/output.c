#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <sys/syscall.h>
#include "cond.h"
char *SCREEN = NULL;
#define WIDTH(p) p->win.ws_col
#define HEIGHT(p) p->win.ws_row

void append_scroll_down(struct pty *pty);
void escape_code(struct pty *pty);
void clear_line(struct pty *pty, int n, int mode)
{
	switch(mode) {
		case 1:
			for(int i=0;i<pty->cx;i++) {
				pty->lines[n][i].character = ' ';
				pty->lines[n][i].attr = DEF_ATTR;
			}
			break;
		case 0:
			for(int i=pty->cx;i<80;i++) {
				pty->lines[n][i].character = ' ';
				pty->lines[n][i].attr = DEF_ATTR;
			}
			break;
		case 2:
			for(int i=0;i<80;i++) {
				pty->lines[n][i].character = ' ';
				pty->lines[n][i].attr = DEF_ATTR;
			}
		break;
	}
}

void clear(struct pty *pty)
{
	for(int x=0;x<80;x++) {
		for(int y=0;y<LINES;y++) {
			pty->lines[y][x].character = ' ';
			pty->lines[y][x].attr = DEF_ATTR;
		}
	}
	pty->cy = pty->sp;
}

void flip(struct pty *pty)
{
	if(pty == current_pty)
		memcpy((void *)SCREEN, &pty->lines[pty->sp][0], 2 * 80 * 25);
}

void flip_line(struct pty *pty, int n)
{
	if(pty == current_pty && n >= pty->sp && n < LINES)
		memcpy(((unsigned short *)SCREEN) + (n - pty->sp) * 80, &pty->lines[n][0], 2 * 80);
}

void append_scroll_down(struct pty *pty)
{
	memmove(&pty->lines[0][0], &pty->lines[1][0], sizeof(struct cell) * (LINES-1) * 80);
	clear_line(pty, LINES-1, 2);
	flip(pty);
	pty->cy--;
}

void scroll_screen_up(struct pty *pty)
{
	memmove(&pty->lines[1][0], &pty->lines[0][0], sizeof(struct cell) * (LINES-1) * 80);
	clear_line(pty, 0, 2);
	flip(pty);
}

int scroll_down(struct pty *pty, int n)
{
	int old = pty->sp;
	if((pty->sp+n) < (LINES - 25))
		pty->sp += n;
	else
		pty->sp = LINES-25;
	if(old != pty->sp) {
		flip(pty);
		update_cursor(pty);
	}
	return pty->sp - old;
}

int scroll_up(struct pty *pty, int n)
{
	int old = pty->sp;
	if((pty->sp - n) > 0)
		pty->sp -= n;
	else
		pty->sp = 0;
	if(old != pty->sp) {
		flip(pty);
		update_cursor(pty);
	}
	return old - pty->sp;
}

void disp_character(struct pty *pty, unsigned char c)
{
	switch(c) {
		case '\t':
			pty->cx = (pty->cx + 8) & ~7;
			break;
		case '\n':
			pty->cy++;
			break;
		case '\r':
			pty->cx = 0;
			break;
		case '\b':
			if(pty->cx > 0) {
				pty->cx--;
			}
			break;
		case 7: /* bell */
			/* TODO */
			break;
		case 27:
			escape_code(pty);
			break;
		default:
			pty->lines[pty->cy][pty->cx].character = c;
			pty->lines[pty->cy][pty->cx].attr = pty->cattr;
			int sy = pty->cy - pty->sp;
			assert(sy < LINES);
			if(sy >= 0 && sy < 25 && pty == current_pty) {
				memcpy(&((unsigned short *)SCREEN)[sy * WIDTH(pty) + pty->cx],
						&pty->lines[pty->cy][pty->cx], 2);
			}
			pty->cx++;
			break;
	}
	if(pty->cx >= WIDTH(pty)) {
		pty->cx = 0;
		pty->cy++;
	}
	if(pty->cy >= LINES) {
		append_scroll_down(pty);
	}
	update_cursor(pty);
}

void escape_cattr(struct pty *pty, int val)
{
	if(val == 0) {
		pty->cattr = DEF_ATTR;
		pty->cursor_invisible = false;
	}
	else if(val == 7) {
		uint8_t attr = (pty->cattr & 0xF) << 4;
		attr |= (pty->cattr & 0xF0) >> 4;
		attr &= ~ATTR_BLINK;
		attr |= (pty->cattr & ATTR_BRIGHT);
		pty->cattr = attr;
	} else if(val == 5) {
		pty->cattr |= ATTR_BLINK;
	} else if(val == 3) {
		pty->cursor_invisible = true;
	} else if(val == 2) {
		pty->cattr &= ~ATTR_BRIGHT;
	} else if(val == 1) {
		pty->cattr |= ATTR_BRIGHT;
	} else if(val >= 30 && val <= 37) {
		int color = val - 30;
		pty->cattr &= 0xF8;
		pty->cattr |= color;
	} else  if(val >= 40 && val <= 47) {
		int color = val - 40;
		pty->cattr &= 0x8F;
		pty->cattr |= color << 4;
	}

}

void clear_screen(struct pty *pty, int mode)
{
	(void)mode;
	for(int i=pty->cy;i < LINES;i++) {
		clear_line(pty, i, 2);
	}
}

void update_cursor(struct pty *pty)
{
	/* TODO: invisible cursor */
	if(pty == current_pty) {
		syscall(93, pty->cx, pty->cy - pty->sp, 0, 0, 0);
	}
}

void escape_command(struct pty *pty, unsigned char cmd, int argc, int args[])
{
	switch(cmd) {
		int x, y;
		case 'm':
			if(argc == 0)
				escape_cattr(pty, 0);
			for(int i=0;i<argc;i++) {
				escape_cattr(pty, args[i]);
			}
			break;
		case 'K':
			/* line clear */
			clear_line(pty, pty->cy, args[0]);
			flip_line(pty, pty->cy);
			break;
		case 'J':
			clear_screen(pty, args[0]);
			flip(pty);
			break;
		case 'H':
			y = args[0];
			x = args[1];
			if(y) y--;
			if(x) x--;
			pty->cx = x;
			pty->cy = pty->sp + y;
			break;
		case 'A':
			if(!args[0]) args[0]++;
			if((pty->cy - args[0]) >= pty->sp)
				pty->cy -= args[0];
			else
				pty->cy = pty->sp;
			break;
		case 'B':
			if(!args[0]) args[0]++;
			if((pty->cy + args[0]) < LINES)
				pty->cy += args[0];
			else
				pty->cy = LINES-1;
			break;
		case 'C':
			if(!args[0]) args[0]++;
			if(pty->cx + args[0] < 80)
				pty->cx += args[0];
			else
				pty->cx = 79;
			break;
		case 'D':
			if(!args[0]) args[0]++;
			if(pty->cx - args[0] > 0)
				pty->cx -= args[0];
			else
				pty->cx = 0;
			break;
		case 'F':
			x = args[0];
			y = pty->cy;
			while(x-->0) {
				append_scroll_down(pty);
			}
			pty->cy = y;
			break;
		case 's':
			pty->s_cx = pty->cx;
			pty->s_cy = pty->cy;
			break;
		case 'S':
			pty->cx = pty->s_cx;
			pty->cy = pty->s_cy;
			break;
		case 'R':
			while(args[0]-->0) {
				scroll_screen_up(pty);
			}
			break;
		case 'P':
			x = args[0];
			if(!x)
				break;
			if(x + pty->cx > 80)
				x = 80 - pty->cx;
			memcpy(&pty->lines[pty->cy][pty->cx], &pty->lines[pty->cy][pty->cx + x],
					sizeof(struct cell) * (80 - (x + pty->cx)));
			y = pty->cx;
			pty->cx = 80 - x;
			clear_line(pty, pty->cy, 0);
			pty->cx = y;
			flip_line(pty, pty->cy);
			break;
		case 'M':
			if(pty->cy < (LINES - 1)) {
				for(int i=0;i<args[0];i++) {
					memcpy(&pty->lines[pty->cy][0], &pty->lines[pty->cy+1][0],
							((LINES - pty->cy)-2) * sizeof(struct cell) * 80);
					clear_line(pty, LINES-1, 2);
				}
			} else {
				clear_line(pty, pty->cy, 2);
			}
			flip(pty);
			break;
		default:
			syslog(LOG_WARNING, "invalid escape sequence command '%c'\n", cmd);
	}
}

/* Allowed escape code types:
 * \e[%C
 * \e[%d%C
 * \e[%d;%dC
 */

#define READ(c) read(pty->masterfd, &c, 1)

void escape_code(struct pty *pty)
{
	char digitbuf[4];
	memset(digitbuf, 0, sizeof(digitbuf));
	int digitpos = 0;

	int args[4];
	memset(args, 0, sizeof(args));
	int argc = 0;
	
	unsigned char c;
	if(READ(c) != 1)
		return;
	if(c == '[') {
		while(1) {
			if(READ(c) != 1)
				return;
			if(isalpha(c)) {
				/* end of escape sequence, we have the command. */
				if(digitpos > 0 && argc < 4) {
					args[argc++] = atoi(digitbuf);
				}
				escape_command(pty, c, argc, args);
				break;
			} else if(isdigit(c) && digitpos < 4) {
				digitbuf[digitpos++] = c;
			} else if(c == ';') {
				if(digitpos > 0 && argc < 4) {
					args[argc++] = atoi(digitbuf);
				}
				memset(digitbuf, 0, sizeof(digitbuf));
				digitpos = 0;
			}
		}
	}
}

void process_output(struct pty *pty)
{
	unsigned char c;
	if(read(pty->masterfd, &c, 1) == 1) {
		disp_character(pty, c);
	}
}

#include <sys/mman.h>
#include <fcntl.h>
static int screen_fd = 0;
void init_screen(void)
{
	syslog(LOG_INFO, "taking control of screen\n");

	screen_fd = open("/dev/vga", O_RDWR);
	if(screen_fd >= 0) {
		SCREEN = mmap(NULL, 0x1000, PROT_WRITE | PROT_READ, MAP_SHARED, screen_fd, 0);
	} else {
		SCREEN = MAP_FAILED;
	}

	if(SCREEN == MAP_FAILED) {
		syslog(LOG_ERR, "failed to map screen\n");
		exit(1);
	}

	syslog(LOG_INFO, "screen mapped\n");
}

