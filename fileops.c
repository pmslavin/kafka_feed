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

fileinfo_t *filequeue_head = NULL;


ssize_t hash_file(const char *path, char **digbuf)
{
	void *fbuf = NULL;

	struct stat sb;
	int fd = open(path, 'r');
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

		int fd = open(f->path, 'r');
		if(fd == -1){
			perror("open");
			return -1;
		}

		if(fstat(fd, &sb) == -1){
			perror("stat");
			return -1;
		}

		f->mtime = sb.st_mtime;
		f->next = NULL;

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
		queue_head = eq;		// Pop consumed eq from eventqueue
		free(eqlast->event);
		free(eqlast);
		eqlast = NULL;
	}
	return fcount;
}


void print_fileinfos(fileinfo_t *f, FILE *dest)
{
	size_t fcount = 1;
	while(f){
		fprintf(dest, "[%u]\t%s (%s) %lu\n", fcount++, f->path, f->digest, f->mtime);
//		fprintf(dest, "[%u]\t%s\n", fcount++, f->path);
//		fprintf(dest, "\tHash: %s\n", f->digest);
//		fprintf(dest, "\tTime: %lu\n\n", f->mtime);
		f = f->next;
	}
}
