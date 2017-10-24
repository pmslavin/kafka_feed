#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "thread.h"
#include "fileops.h"
#include "kafkaops.h"
#include "utils.h"


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
		- Worker raises dqcond
		- Worker releases dqm
*/
size_t			 file_nthreads = 8;
size_t			 done_nthreads = 4;
static thread_info_t *ftinfo = NULL;
static thread_info_t *dtinfo = NULL;
pthread_mutex_t	 fqmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t	 dqmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	 fqcond  = PTHREAD_COND_INITIALIZER;
pthread_cond_t	 dqcond  = PTHREAD_COND_INITIALIZER;
int				 work_available = 0;
int				 work_complete  = 0;


void *file_worker(void *arg)
{
	thread_info_t *t = (thread_info_t *)arg;
	fileinfo_t *f = NULL, *dq = NULL, *dqprev = NULL;
	cdrmsg_t *cdrq_head = NULL;
	char log_time[24];

	while(1){
		while(!work_available){
#ifdef THREAD_DEBUG
			fprintf(stderr, "fq thread %u waiting...\n", t->thread_num);
#endif
			pthread_cond_wait(&fqcond, &fqmutex);
		}

		if(filequeue_head){
			f = filequeue_head;
			filequeue_head = filequeue_head->next;
		}
		if(!filequeue_head)
			work_available = 0;
		pthread_mutex_unlock(&fqmutex);
		if(f){
			f->next = NULL;
			isotime(log_time);
			size_t cdr_count = enqueue_cdr_msgs(&cdrq_head, f);
			fprintf(stderr, "[%s (%u)] %s %u bytes %u msgs %lu %s\n", log_time, t->thread_num, f->path, (unsigned int)f->size, cdr_count, f->mtime, f->digest);
			publish_cdrqueue(&cdrq_head);
			pthread_mutex_lock(&dqmutex);
			/* put on dq */
			dq = donequeue_head;
			while(dq){
				dqprev = dq;
				dq = dq->next;
			}
			if(dqprev)
				dqprev->next = f;
			else
				donequeue_head = f;
			work_complete = 1;
			pthread_cond_signal(&dqcond);
			pthread_mutex_unlock(&dqmutex);
			f = NULL;
		}
#ifdef THREAD_DEBUG
		fprintf(stderr, "fq thread %u complete...\n", t->thread_num);
#endif
	}
	return NULL;
}


void *done_worker(void *arg)
{
	thread_info_t *t = (thread_info_t *)arg;
	fileinfo_t *f = NULL;
	char log_time[24];

	while(1){
		while(!work_complete){
#ifdef THREAD_DEBUG
			fprintf(stderr, "dq thread %u waiting...\n", t->thread_num);
#endif
			pthread_cond_wait(&dqcond, &dqmutex);
		}
		if(donequeue_head){
			f = donequeue_head;
			donequeue_head = donequeue_head->next;
		}
		if(!donequeue_head)
			work_complete = 0;
		pthread_mutex_unlock(&dqmutex);
		if(f){
			/* do work */
			char *fname = strrchr(f->path, '/');
			if(fname)
				fname++;
			size_t dest_sz = strlen(complete_dir)+strlen(fname)+2;
			char *fdest = malloc(dest_sz);
			if(!fdest){
				perror("malloc fdest");
			}else{
				snprintf(fdest, dest_sz, "%s/%s", complete_dir, fname);
				int ret = rename(f->path, fdest);
				if(ret){
					if(errno == EXDEV)
						perror("rename across filesystems");
					else
						perror("rename");
				}
				f->next = NULL;
				isotime(log_time);
				fprintf(stderr, "[%s (%u)] %s processed to %s \n", log_time, t->thread_num, f->path, fdest);
				free(fdest);
			}
			free_fileinfo(f);
			f = NULL;
		}
	}
	return NULL;
}


int create_threads(void)
{
	/* filequeue workers */
	ftinfo = calloc(file_nthreads, sizeof(thread_info_t));
	if(!ftinfo){
		perror("calloc ftinfo");
		return -1;
	}

	for(size_t idx=0; idx<file_nthreads; idx++){
		ftinfo[idx].thread_num = idx;
		ftinfo[idx].arg		  = NULL;
		int ret = pthread_create(&ftinfo[idx].thread_id, NULL, file_worker, &ftinfo[idx]);
		if(ret)
			return -1;
	}

	/* donequeue workers */
	dtinfo = calloc(done_nthreads, sizeof(thread_info_t));
	if(!dtinfo){
		perror("calloc dtinfo");
		return -1;
	}

	for(size_t idx=0; idx<done_nthreads; idx++){
		dtinfo[idx].thread_num = idx;
		dtinfo[idx].arg		  = NULL;
		int ret = pthread_create(&dtinfo[idx].thread_id, NULL, done_worker, &dtinfo[idx]);
		if(ret)
			return -1;
	}

	return 0;
}


int destroy_threads(void)
{
	free(ftinfo);
	free(dtinfo);
	return 0;
}
