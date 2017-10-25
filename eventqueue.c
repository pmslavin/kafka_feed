#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "eventqueue.h"

eventqueue_t *evqueue_head = NULL;
size_t eventqueue_size = 0;


size_t enqueue_events(eventqueue_t *q, const char *buf, size_t buf_sz)
{
	(void)q;	//unused

	size_t idx = 0, qecount = 0;
	struct inotify_event *event = NULL;

	while(idx < buf_sz){
		eventqueue_t *qlast = NULL;
		event = (struct inotify_event *)(buf+idx);
		size_t inc = offsetof(struct inotify_event, name) + event->len;
		idx += inc;
		if(event->mask & IN_ISDIR)
			continue;

		qecount++;
		while(q){
			qlast = q;
			q = q->next;
		}

		q = malloc(sizeof(eventqueue_t));
		q->next  = NULL;
		q->event = malloc(inc);
		memcpy(q->event, event, inc);
		if(qlast)
			qlast->next = q;
		else
			evqueue_head  = q;

#ifdef DEBUG
		fprintf(stderr, "\tqecount: %u  idx: %d\n", qecount, idx);
#endif
	}

	return qecount;
}


void print_queue(eventqueue_t *q, FILE *dest)
{
	size_t count = 1;
	while(q){
		fprintf(dest, "[%u]\tfile: %s", count++, q->event->name);
		if(q->event->mask & IN_CREATE)
			fprintf(dest, " created\n");
		if(q->event->mask & IN_MOVED_TO)
			fprintf(dest, " moved into\n");
		if(q->event->mask & IN_CLOSE_WRITE)
			fprintf(dest, " close write\n");
		if(q->event->mask & IN_MODIFY)
			fprintf(dest, " modify\n");

		q = q->next;
	}
}


void free_queue_item(eventqueue_t *q)
{
	free(q->event);
	free(q);
	q = NULL;
}
