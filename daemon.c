/*
	Daemon, Syslog and Signals ops
*/
#define _POSIX_SOURCE	// meh kill
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>

#include "daemon.h"

#define WRITE_PIDFILE

#define RUN_AS_USER "nobody"
#define	PIDDIR		"/var/run/monitor"
#define PIDFILE		"monitor.pid"


static void child_handler(int signum)
{
	switch(signum){
		case SIGUSR1:
			exit(0);
			break;
		default:
			exit(-1);
			break;
	}
}


int daemonize(void)
{
	pid_t pid, ppid, sid;

	if(getppid() == 1)
		return -1;

	/* Drop privileges if root */
	if( getuid() == 0 || geteuid() == 0){
		errno = 0;
		struct passwd *pw = getpwnam(RUN_AS_USER);
		if(pw)
			setuid(pw->pw_uid);
		else
			perror("getpwnam: " RUN_AS_USER);
	}

	signal(SIGUSR1, child_handler);
	signal(SIGCHLD, child_handler);
	signal(SIGALRM, child_handler);

	pid = fork();
	if(pid < 0){
		perror("fork");
		return -1;
	}
	/* Parent */
	if(pid > 0){
		alarm(3);
		pause();
		/* This never happens */
		exit(0);
	}
	/* Child */
	pid  = getpid();
	ppid = getppid();
	sid  = setsid();
	if(sid < 0){
		perror("setsid");
		exit(-1);
	}

	errno = 0;
	if(chdir("/")){
		perror(strerror(errno));
		exit(-1);
	}
	umask(0113);
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

#ifdef WRITE_PIDFILE
	/* Assumes writeable PIDDIR */
	int fd = open(PIDDIR "/" PIDFILE, O_CREAT | O_WRONLY | O_TRUNC);
	if(fd == -1){
		perror("Unable to write pid: " PIDFILE);
	}else{
		int ret = write(fd, &pid, sizeof(pid));
		if(ret == -1)
			perror("write pid");
		close(fd);
	}
#endif

	kill(ppid, SIGUSR1);

	return 0;
}
