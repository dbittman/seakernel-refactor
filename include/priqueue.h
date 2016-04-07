#pragma once

#include <lib/linkedlist.h>

struct priqueue_node {
	struct linkedentry entry;
};

#define PRIQUEUE_LEVELS 32

struct priqueue {
	struct spinlock lock;
	int levels, curhighest;
	struct linkedlist lists[PRIQUEUE_LEVELS];
};

void *priqueue_pop(struct priqueue *pq);
void priqueue_insert(struct priqueue *pq, struct priqueue_node *node, void *data, int pri);
void priqueue_create(struct priqueue *pq, int levels);

