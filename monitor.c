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
#include "daemon.h"

#define INBUF_SZ 4096

typedef void (*sighandler_t)(int);



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
	fprintf(stderr, "[%s] Ending watch on %s (%u)\n", log_time, params[proc_id].src, monitor_pid);
	destroy_threads();

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
/* Each dir proc needs to do this now... */
/*	isotime(log_time);
	fprintf(stderr, "[%s] Reinitialising Kafka producer...\n", log_time);
	close_kafka_producer();
	init_kafka_producer();
*/
}



int main(int argc, char *argv[])
{
	(void)argc;	// unused
	(void)argv;	// unused

	daemonize();
	fork_children(2);
	init_logger();

	if(master){
		int active = 1;
		signal(SIGINT, sigint_tidy);
		signal(SIGHUP, sighup_reload);
		while(active){
			pause();
			fprintf(stderr, "Master receives signal.\n");
		}
		fprintf(stderr, "Master exits on signal.\n");
		exit(EXIT_SUCCESS);
	}

	struct pollfd fd;

	int infd = inotify_init();
	if(infd == -1){
		perror("inotify_init");
		return -1;
	}

	int wd = inotify_add_watch(infd, params[proc_id].src, IN_CLOSE_WRITE | IN_MOVED_TO);
	if(wd == -1){
		perror("inotify_add_watch");
		fprintf(stderr, "Unable to watch dir: %s\n", params[proc_id].src);
		return -1;
	}

	fd.fd	  = infd;
	fd.events = POLLIN;

	int rk_ret = init_kafka_producer();
	if(rk_ret == -1){
		fprintf(stderr, "Unable to initialise Kafka: %d\n", rk_ret);
		return -1;
	}

	create_threads();

	monitor_pid = getpid();
	char init_time[ISO_TIME_SZ];
	isotime(init_time);
	fprintf(stderr, "[%s] Watching %s (%u)\n", init_time, params[proc_id].src, monitor_pid);

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

#ifdef DEBUG
			fprintf(stderr, "%d bytes of IN events\n", count);
#endif
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

		eventqueue_size += enqueue_events(evqueue_head, inbuf, in_count);
#ifdef DEBUG
		fprintf(stderr, "Event queue: %u items\n", eventqueue_size);
		print_queue(evqueue_head, stderr);
#endif
		pthread_mutex_lock(&fqmutex);
		filequeue_size += enqueue_files(filequeue_head, evqueue_head, params[proc_id].src);
		work_available = 1;
		pthread_cond_broadcast(&fqcond);
		pthread_mutex_unlock(&fqmutex);
#ifdef DEBUG
		fprintf(stderr, "File queue: %u items\n", filequeue_size);
#endif
//		print_fileinfos(filequeue_head, stderr);
//		print_cdrmsgs(cdrmsgqueue_head, stderr);
	}

	close(infd);
	return 0;
}
