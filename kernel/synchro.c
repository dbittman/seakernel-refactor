#include <blocklist.h>
#include <thread.h>
#include <printk.h>
void sleepflag_create(struct sleepflag *s)
{
	blocklist_create(&s->bl);
	s->flag = 1;
}

void sleepflag_sleep(struct sleepflag *s)
{
	if(s->flag >= 2) {
		s->flag--;
		return;
	}
	assert(s->flag >= 0);
	struct blockpoint bp;
	blockpoint_create(&bp, 0, 0);
	blockpoint_startblock(&s->bl, &bp);
	if(--s->flag == 0) {
		schedule();
	}
	enum block_result res = blockpoint_cleanup(&bp);
	if(res != BLOCK_RESULT_UNBLOCKED && res != BLOCK_RESULT_BLOCKED) {
		s->flag++;
	}
}

void sleepflag_wake(struct sleepflag *s)
{
	s->flag++;
	blocklist_unblock_all(&s->bl);
}

