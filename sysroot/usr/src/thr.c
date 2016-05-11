#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#define N 3
#define P 1
static _Atomic int done[N] = { 0 };

pthread_mutex_t lock;
int t = 0;
void sync_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&lock);
    vprintf(format, args);
    t++;
    pthread_mutex_unlock(&lock);

    va_end(args);
}

void *func(void *d)
{
	int id = (int)(long)d;
	for(int i=0;i<P;i++) {
		sync_printf("Hello, World from thread %d!\n", id);
	}
	done[id]++;
	return (void *)(long)id;
}

int main()
{
	pthread_t thread[N];
	fprintf(stderr, "Creating...\n");
	pthread_mutex_init(&lock, 0);
	
	for(int i=0;i<N;i++)
		pthread_create(&thread[i], NULL, func, (void *)(long)i);
	int r;
	for(int i=0;i<N;i++) {
		void *d;
		pthread_join(thread[i], &d);
		r = (int)(long)d;
		assert(done[r] == 1);
		assert(r == i);
		sync_printf("Joined: %d!\n", r);
	}
	assert(t == N*P*2);
	return 0;
}

