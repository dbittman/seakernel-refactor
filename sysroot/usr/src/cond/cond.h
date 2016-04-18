#define LINES 1000
#define MAX_TERMS 12

#define DEF_ATTR 0x7

#define ATTR_BLINK (1 << 7)
#define ATTR_BRIGHT (1 << 3)

#include <stdint.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdbool.h>

struct cell {
	uint8_t character;
	uint8_t attr;
};

struct pty {
	int masterfd;
	struct cell lines[LINES][80];
	int cx, cy, sp, s_cy, s_cx;
	bool cursor_invisible;
	uint8_t cattr;
	struct termios term;
	struct winsize win;
};

extern struct pty *ptys[MAX_TERMS];
extern struct pty *current_pty;
extern int keyfd;

void read_keyboard(void);
void process_output(struct pty *pty);
void init_screen(void);
void clear(struct pty *pty);
int scroll_down(struct pty *pty, int n);
int scroll_up(struct pty *pty, int n);
void update_cursor(struct pty *pty);
void switch_console(int);
void flip(struct pty *pty);
void update_cursor(struct pty *pty);
bool pty_is_icanon(struct pty *pty);
