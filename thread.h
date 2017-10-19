#ifndef __THREAD_H__
#define __THREAD_H__

#include <pthread.h>

typedef struct thread_info{
	pthread_t	 thread_id;
	pid_t		 thread_num;
	void		*arg;
}thread_info_t;



void *file_worker(void *);
int	  create_threads(void);
int	  destroy_threads(void);

extern size_t nthreads;
extern thread_info_t   *tinfo;
extern pthread_mutex_t  fqmutex;
extern pthread_mutex_t  dqmutex;
extern pthread_cond_t   fqcond;
extern int 				work_available;

#endif
