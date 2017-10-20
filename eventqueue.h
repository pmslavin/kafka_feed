#ifndef __EVENTQUEUE_H__
#define __EVENTQUEUE_H__

#include <stdio.h>
#include <stddef.h>
#include <sys/inotify.h>


typedef struct queue_t{
	struct queue_t		 *next;
	struct inotify_event *event;
}eventqueue_t;

extern eventqueue_t *evqueue_head;

size_t enqueue_events(eventqueue_t *, const char *, size_t);
void print_queue(eventqueue_t *, FILE *);
void free_queue_item(eventqueue_t *);

#endif
