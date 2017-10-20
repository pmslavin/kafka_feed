#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "thread.h"
#include "fileops.h"
#include "kafkaops.h"
#include "utils.h"

size_t nthreads = 4;
thread_info_t *tinfo = NULL;

/*	Schedule:
		- Master thread acquires fqm
		- Master thread places work on fq
		- Master thread releases fqm
		- Master thread raises fqcond
		- Worker acquires fqm
		- Worker tests and possibly unsets fqcond
		- Worker takes work from fq
		- Worker releases fqm
		- Worker performs work
		- Worker acquires dqm
		- Worker places completed work on dq
		- Worker releases dqm
*/
pthread_mutex_t	fqmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t	dqmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	fqcond  = PTHREAD_COND_INITIALIZER;
int	work_available = 0;


void *file_worker(void *arg)
{
	thread_info_t *t = (thread_info_t *)arg;
	fileinfo_t *f = NULL;
	cdrmsg_t *cdrq_head = NULL;
	char log_time[24];

	while(1){
		while(!work_available){
			fprintf(stderr, "thread %u waiting...\n", t->thread_num);
			pthread_cond_wait(&fqcond, &fqmutex);
		}

		if(filequeue_head){
			f = filequeue_head;
			filequeue_head = filequeue_head->next;
		}
		if(!filequeue_head){
			work_available = 0;
			pthread_cond_broadcast(&fqcond);
		}
		pthread_mutex_unlock(&fqmutex);
		if(f){
			f->next = NULL;
			isotime(log_time);
			size_t cdr_count = enqueue_cdr_msgs(&cdrq_head, f);
			fprintf(stderr, "[%s (%u)] %s %u bytes %u msgs %lu %s\n", log_time, t->thread_num, f->path, (unsigned int)f->size, cdr_count, f->mtime, f->digest);
		}
		publish_cdrqueue(&cdrq_head);
		pthread_mutex_lock(&dqmutex);
		/* put on dq */
		sleep(1);
		pthread_mutex_unlock(&dqmutex);
		fprintf(stderr, "thread %u complete...\n", t->thread_num);
	}
	return NULL;
}


int create_threads(void)
{
	tinfo = calloc(nthreads, sizeof(thread_info_t));
	if(!tinfo){
		perror("calloc tinfo");
		return -1;
	}

	for(size_t idx=0; idx<nthreads; idx++){
		tinfo[idx].thread_num = idx;
		tinfo[idx].arg		  = NULL;
		int ret = pthread_create(&tinfo[idx].thread_id, NULL, file_worker, &tinfo[idx]);
		if(ret)
			return -1;
	}

	return nthreads;
}


int destroy_threads(void)
{
	free(tinfo);
	return 0;
}
