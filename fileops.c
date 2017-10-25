#define _BSD_SOURCE // madvise
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "eventqueue.h"
#include "fileops.h"
#include "kafkaops.h"
#include "utils.h"

fileinfo_t *filequeue_head   = NULL;
fileinfo_t *donequeue_head   = NULL;
cdrmsg_t   *cdrmsgqueue_head = NULL;
size_t	    filequeue_size   = 0;	// Requires fqmutex


ssize_t hash_file(const char *path, char **digbuf)
{
	void *fbuf = NULL;

	struct stat sb;
	int fd = open(path, O_RDONLY);
	if(fd == -1){
		perror("open");
		return -1;
	}

	if(fstat(fd, &sb) == -1){
		perror("stat");
		return -1;
	}

	/* mmap will EINVAL for 0 len file */
	if(sb.st_size > 0){
		fbuf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if(!fbuf){
			perror("mmap");
			return -1;
		}
/* Non-posix; attempt only when present */
#ifdef _BSD_SOURCE
		madvise(fbuf, sb.st_size, MADV_SEQUENTIAL | MADV_WILLNEED);
#endif
	}
	close(fd);

	/* Still perform hash if zero-length file */
	unsigned char *hbuf = malloc(SHA256_DIGEST_LENGTH);
	SHA256(fbuf, sb.st_size, hbuf);

	*digbuf = malloc(2*SHA256_DIGEST_LENGTH);
	for(int i=0; i<SHA256_DIGEST_LENGTH; i++)
		snprintf((*digbuf)+(2*i), 3, "%02x", hbuf[i]);

	munmap(fbuf, sb.st_size);
	free(hbuf);
	return SHA256_DIGEST_LENGTH;
}


size_t enqueue_files(fileinfo_t *fq, eventqueue_t *eq, const char *dir)
{
	fileinfo_t *lastf = NULL;
	size_t fcount = 0;

	size_t dirlen = strlen(dir);
	struct stat sb;
	eventqueue_t *eqlast = NULL;

	while(eq){
		eqlast = eq;
		fileinfo_t *f = malloc(sizeof(fileinfo_t));
		fcount++;
		size_t pathlen = dirlen + strlen(eq->event->name) + 2;	// Plus '/' and NULL
		f->path = calloc(1, pathlen);
		snprintf(f->path, pathlen, "%s/%s", dir, eq->event->name);

		int hash_ret = hash_file(f->path, &(f->digest));
		if(hash_ret == -1){
			perror("hash_file");
			return -1;
		}

		int fd = open(f->path, O_RDONLY);
		if(fd == -1){
			perror("open");
			return -1;
		}

		if(fstat(fd, &sb) == -1){
			perror("stat");
			return -1;
		}

		f->size	 = sb.st_size;
		f->mtime = sb.st_mtime;
		f->next  = NULL;

		while(fq){
			lastf = fq;
			fq	  = fq->next;
		}

		if(lastf){
			lastf->next = f;
		}else{
			filequeue_head = f;
		}

		fq = filequeue_head;	// Always restart fq search from q head
		eq = eq->next;
		evqueue_head = eq;		// Pop consumed eq from eventqueue
		free(eqlast->event);
		free(eqlast);
		eqlast = NULL;
		eventqueue_size--;
	}
	return fcount;
}


void print_fileinfos(fileinfo_t *f, FILE *dest)
{
	size_t fcount = 0;
	fileinfo_t *last = NULL;
	char log_time[24];
	while(f){
		++fcount;
		isotime(log_time);
		size_t cdr_count = enqueue_cdr_msgs(&cdrmsgqueue_head, f);
		fprintf(dest, "[%s] %s %u bytes %u msgs %lu %s\n", log_time, f->path, (unsigned int)f->size, cdr_count, f->mtime, f->digest);
//		(void)cdr_count;	// unused

/*		fprintf(stderr, "data: %s  count: %u\n", cdrmsgqueue_head->msg, cdr_count);
		char *msg_buf = form_file_msg(f, &msg_size);
		fprintf(stderr, "%s msg_size: %u\n", msg_buf, msg_size);
		publish(msg_buf, msg_size);
		free(msg_buf);
*/
		last = f;
		f = f->next;
		free_fileinfo(last);
		last = NULL;
	}
	filequeue_head = NULL;
//	fprintf(dest, "file count: %u\n", fcount);
}


void print_cdrmsgs(cdrmsg_t *q, FILE *dest)
{
	(void)dest;	// unused
	size_t msg_count = 0;
	cdrmsg_t *last = NULL;
	while(q){
//		fprintf(dest, "[%3u] %s\n", ++msg_count, q->msg);
		++msg_count;
		publish(q->msg, strlen(q->msg));
		last = q;
		q = q->next;
		free_cdrmsg(last);
		last = NULL;
	}
//	fprintf(dest, "msg count: %u\n", msg_count);
	flush_kafka_buffer(5);
	cdrmsgqueue_head = NULL;
}


void free_cdrmsg(cdrmsg_t *c)
{
	free(c->msg);
	c->msg = NULL;
	free(c);
}


void free_fileinfo(fileinfo_t *f)
{
	free(f->path);
	free(f->digest);
	free(f);
}
