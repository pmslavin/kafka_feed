#ifndef __UTILS_H__
#define __UTILS_H__

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define ISO_TIME_SZ 24

extern pid_t monitor_pid;

size_t isotime(char *);

#endif
