#ifndef __FILEOPS_H__
#define __FILEOPS_H__

#include <time.h>
#include <stdio.h>

typedef struct fileinfo{
	struct fileinfo *next;
	char 			*path;
	char 			*digest;
	time_t	 		 mtime;
}fileinfo_t;

extern fileinfo_t *filequeue_head;

ssize_t hash_file(const char *, char **);
size_t enqueue_files(fileinfo_t *, eventqueue_t *, const char *);
void print_fileinfos(fileinfo_t *, FILE *);

#endif
