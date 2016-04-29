#pragma once

void proc_destroy(const char *path);
void proc_create(const char *path, ssize_t (*call)(void *data, int, size_t, size_t, char *), void *data);

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


