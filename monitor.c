#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stddef.h>
#include <stdlib.h>

#include "eventqueue.h"
#include "fileops.h"


const char *watch_dir = "/tmp/monitor";

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
		return -1;
	}

	fd.fd	  = infd;
	fd.events = POLLIN;
	size_t qecount = 0;;

/*
	char *hbuf = NULL;
	int hsize = hash_file("/home/pslavin/.bashrc", &hbuf);
	fprintf(stderr, ".bashrc: %s\n", hbuf);
	free(hbuf);
*/

	fprintf(stderr, "Watching %s:\n", watch_dir);

	while(1){
		int pollret = poll(&fd, 1, -1);
		if(pollret == -1){
			perror("poll");
			return -1;
		}

		char	buf[1024];
		int		idx = 0, in_count = 0;

		if(pollret && fd.revents & POLLIN){
			size_t count;
			int iret = ioctl(infd, FIONREAD, &count);
			if(iret == -1){
				perror("ioctl");
				return -1;
			}

			fprintf(stderr, "%d bytes of IN events\n", count);
			in_count = count;

			int ret;
			while( count>0 && (ret = read(infd, &buf[idx], count)) != 0){
				if(ret == -1){
					perror("read");
					return -1;
				}
				count -= ret;
				idx	  += ret;
			}
		}

		qecount += enqueue_events(queue_head, buf, in_count);
		print_queue(queue_head, stderr);
		enqueue_files(filequeue_head, queue_head, watch_dir);
		print_fileinfos(filequeue_head, stderr);

	}

	close(infd);

	return 0;
}
