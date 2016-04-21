#include <stddef.h>
#include <string.h>
int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    const unsigned char* vptr1 = (const unsigned char*)ptr1;
    const unsigned char* vptr2 = (const unsigned char*)ptr2;
    while (num) {
        if (*vptr1 > *vptr2) return 1;
        else if (*vptr1 < *vptr2) return -1;
        vptr1++; vptr2++; num--;
    }
    return 0;
}

void* memchr(const void* ptr, int value, size_t num) {
    const unsigned char* vptr = (const unsigned char*)ptr;
    while (num) {
        if (*vptr == (unsigned char)value)
            return ((void*)vptr);
        vptr++; num--;
    }
    return NULL;
}

char *strcat(char *str, const char *cat)
{
	str += strlen(str);
	do {
		*str++ = *cat;
	} while(*cat++);
	return str;
}

int strcmp(const char* str1, const char* str2) {
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);

	int cmpResult = memcmp(str1, str2, (len1 < len2) ? len1 : len2);
	if (cmpResult != 0)
	    return cmpResult;

	if (len1 > len2)
	    return 1;
	else if (len1 < len2)
	    return -1;

	return 0;
}

const char *strchrc(const char *str, char v)
{
	while(*str && *str != v) {
		str++;
	}
	if(*str == v)
		return str;
	return NULL;
}

const char *strrchrc(const char *str, char v)
{
	const char *s = str + strlen(str) - 1;
	while(s != str) {
		if(*s == v)
			return s;
		s--;
	}
	if(*s == v)
		return s;
	return NULL;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	return memcpy(dest, src, n);
}

void *memset(void *s, int c, size_t n)
{
	unsigned char *b = s;
	while(n--)
		*b++ = c;
	return s;
}
int strncmp(const char* s1, const char* s2, size_t n) {
	        return memcmp(s1, s2, n);
}
size_t strlen(const char *s)
{
	size_t c = 0;
	while(*s++) c++;
	return c;
}

void* memcpy(void* destination, const void* source, size_t num) {
    const unsigned char* vsource = (const unsigned char*)source;
    unsigned char* vdestination = (unsigned char*)destination;
    while (num >= 1) {
        *vdestination = *vsource;
        vsource++; vdestination++; num--;
    }
    return destination;
}

void* memmove(void* destination, const void* source, size_t num) {
    const unsigned char* sp;
    unsigned char* dp;
    if (destination < source) {
        sp = (const unsigned char*)source; dp = (unsigned char*)destination;
        while (num) { *dp = *sp; dp++; sp++; num--; }
    } else {
        while (num) {
            sp = (unsigned char*)source + num - 1;
            dp = (unsigned char*)destination + num - 1;
            *dp = *sp;
            num--;
        }
    }
    return destination;
}

long strtol(char *str, char **end, int base)
{
	long tmp = 0;
	bool neg = false;
	if(*str == '-') {
		neg = true;
		str++;
	}
	if(*str == '+')
		str++;

	while(*str) {
		if(*str >= '0' && *str <= '0' + (base - 1)) {
			tmp *= base;
			tmp += *str - '0';
		} else if(*str >= 'a' && *str < 'a' + (base - 10)) {
			tmp *= base;
			tmp += *str - 'a';
		} else if(*str >= 'A' && *str < 'A' + (base - 10)) {
			tmp *= base;
			tmp += *str - 'A';
		} else {
			break;
		}

		str++;
	}

	if(end) *end = str;

	return neg ? -tmp : tmp;
}

