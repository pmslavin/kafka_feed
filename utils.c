#include "utils.h"

pid_t monitor_pid;


size_t isotime(char *tbuf)
{
	time_t time_arg;
	time(&time_arg);
	struct tm *init_tm;
	init_tm = localtime(&time_arg);
	strftime(tbuf, ISO_TIME_SZ, "%F %T", init_tm);

	return strlen(tbuf);
}
