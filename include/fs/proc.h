#pragma once

void proc_destroy(const char *path);
void proc_create(const char *path, ssize_t (*call)(void *data, int, size_t, size_t, char *), void *data);
ssize_t proc_read_data(uint32_t id, size_t off, size_t len, char *b);
#include <string.h>
#define PROCFS_PRINTF(offset,length,buf,current,format...) \
	do { \
		char line[1024]; \
		int add = snprintf(line, 1024, format); \
		if(current + add > offset && current < (offset + length)) { \
			size_t linestart = current > offset ? 0 : (offset - current); \
			size_t bufstart  = current > offset ? (current - offset) : 0; \
			size_t amount = add - linestart; \
			if(amount > ((offset + length) - current)) \
				amount = (offset + length) - current; \
			memcpy(buf + bufstart, line + linestart, amount); \
			current += amount; \
		} else if(current + add <= offset) { \
			offset -= add; \
		} \
	} while(0);


ssize_t _proc_read_int(void *data, int rw, size_t off, size_t len, char *buf);
