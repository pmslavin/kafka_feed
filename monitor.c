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

#include "eventqueue.h"
#include "fileops.h"
#include "kafkaops.h"

#define INBUF_SZ 4096

typedef void (*sighandler_t)(int);
const char *watch_dir = "/tmp/monitor";

void sigint_tidy(int arg)
{
	(void)arg;	// unused
	fprintf(stderr, "\rInterrupt...\n");
	fprintf(stderr, "Closing Kafka producer...\n");
	close_kafka_producer();
	fprintf(stderr, "Ending watch on %s\n", watch_dir);

	exit(0);
}


void sighup_reload(int arg)
{
	(void)arg;	// unused
	fprintf(stderr, "Caught HUP...\n");
	fprintf(stderr, "Reloading config...\n");
	fprintf(stderr, "Reinitialising Kafka producer...\n");
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
		fprintf(stderr, "unable to initialise Kafka: %d\n", rk_ret);
		return -1;
	}

/*
	struct sigaction sa;
	sa.sa_handler = sighup_reload;
	sa.sa_flags   = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
*/
	signal(SIGINT, sigint_tidy);
	signal(SIGHUP, sighup_reload);

/* Test hash
	char *hbuf = NULL;
	int hsize = hash_file("/home/pslavin/.bashrc", &hbuf);
	fprintf(stderr, ".bashrc: %s\n", hbuf);
	free(hbuf);
*/

	fprintf(stderr, "Watching %s...\n", watch_dir);

	while(1){
		int pollret = poll(&fd, 1, -1);
		if(pollret == -1){
			if(errno == EINTR)
				continue;
			perror("poll");
			return -1;
		}

		char	buf[INBUF_SZ];
		int		idx = 0, in_count = 0;

		if(pollret && fd.revents & POLLIN){
			size_t count;
			int iret = ioctl(infd, FIONREAD, &count);
			if(iret == -1){
				perror("ioctl");
				return -1;
			}

//			fprintf(stderr, "%d bytes of IN events\n", count);
			in_count = count;

			int ret;
			while( count>0 && (ret = read(infd, &buf[idx], count)) != 0){
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

		qecount += enqueue_events(queue_head, buf, in_count);
#ifdef DEBUG
		print_queue(queue_head, stderr);
#endif
		enqueue_files(filequeue_head, queue_head, watch_dir);
		print_fileinfos(filequeue_head, stderr);
		print_cdrmsgs(cdrmsgqueue_head, stderr);
	}

	close(infd);
	return 0;
}
