//#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>

#include "utils.h"
#include "eventqueue.h"
#include "fileops.h"
#include "kafkaops.h"
#include "thread.h"

#define INBUF_SZ 4096

typedef void (*sighandler_t)(int);
const char *watch_dir = "/tmp/monitor";



void sigint_tidy(int arg)
{
	(void)arg;	// unused
	char log_time[24];
	isotime(log_time);
	fprintf(stderr, "\r[%s] Interrupt...\n", log_time);
	isotime(log_time);
	fprintf(stderr, "[%s] Closing Kafka producer...\n", log_time);
	close_kafka_producer();
	isotime(log_time);
	fprintf(stderr, "[%s] Ending watch on %s (%u)\n", log_time, watch_dir, monitor_pid);

	exit(0);
}


void sighup_reload(int arg)
{
	(void)arg;	// unused
	char log_time[24];
	isotime(log_time);
	fprintf(stderr, "\r[%s] Caught HUP...\n", log_time);
	isotime(log_time);
	fprintf(stderr, "[%s] Reloading config...\n", log_time);
	isotime(log_time);
	fprintf(stderr, "[%s] Reinitialising Kafka producer...\n", log_time);
	close_kafka_producer();
	init_kafka_producer();
}



int main(int argc, char *argv[])
{
	(void)argc;	// unused
	(void)argv;	// unused

	struct pollfd fd;

	int infd = inotify_init();
	if(infd == -1){
		perror("inotify_init");
		return -1;
	}

	int wd = inotify_add_watch(infd, watch_dir, IN_CREATE | IN_MOVED_TO);
	if(wd == -1){
		perror("inotify_add_watch");
		fprintf(stderr, "Unable to watch dir: %s\n", watch_dir);
		return -1;
	}

	fd.fd	  = infd;
	fd.events = POLLIN;
	size_t qecount = 0;;

	int rk_ret = init_kafka_producer();
	if(rk_ret == -1){
		fprintf(stderr, "Unable to initialise Kafka: %d\n", rk_ret);
		return -1;
	}
	signal(SIGINT, sigint_tidy);
	signal(SIGHUP, sighup_reload);

/* Test hash
	char *hbuf = NULL;
	int hsize = hash_file("/home/pslavin/.bashrc", &hbuf);
	fprintf(stderr, ".bashrc: %s\n", hbuf);
	free(hbuf);
*/

	create_threads();

	monitor_pid = getpid();
	char init_time[ISO_TIME_SZ];
	isotime(init_time);
	fprintf(stderr, "[%s] Watching %s (%u)\n", init_time, watch_dir, monitor_pid);

	while(1){
		int pollret = poll(&fd, 1, -1);
		if(pollret == -1){
			if(errno == EINTR)
				continue;
			perror("poll");
			return -1;
		}

		char	inbuf[INBUF_SZ];
		int		idx = 0;
		size_t	in_count = 0;

		if(pollret && fd.revents & POLLIN){
			size_t count;
			int iret = ioctl(infd, FIONREAD, &count);
			if(iret == -1){
				perror("ioctl");
				return -1;
			}

//			fprintf(stderr, "%d bytes of IN events\n", count);
			in_count = count;

			ssize_t ret;
			while( count>0 && (ret = read(infd, inbuf+idx, count)) != 0){
				if(ret == -1){
					if(errno == EINTR)
						continue;
					perror("read");
					return -1;
				}
				count -= ret;
				idx	  += ret;
			}
		}

		qecount += enqueue_events(queue_head, inbuf, in_count);
#ifdef DEBUG
		print_queue(queue_head, stderr);
#endif
		pthread_mutex_lock(&fqmutex);
		enqueue_files(filequeue_head, queue_head, watch_dir);
		work_available = 1;
		pthread_cond_broadcast(&fqcond);
		pthread_mutex_unlock(&fqmutex);
//		print_fileinfos(filequeue_head, stderr);
//		print_cdrmsgs(cdrmsgqueue_head, stderr);
	}

	close(infd);
	return 0;
}
