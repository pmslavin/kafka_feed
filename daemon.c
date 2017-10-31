/*
	Daemon, Syslog and Signals ops
*/
#define _POSIX_C_SOURCE 200809L	// dprintf, kill
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
#include <wait.h>

#include "daemon.h"

#define WRITE_PIDFILE

#define RUN_AS_USER "nobody"
#define PIDDIR		"/var/run/monitor"
#define PIDFILE		"monitor.pid"

#define NUM_CHLD 2

chld_params_t params[NUM_CHLD] = {
			{ "/tmp/monitor", "/tmp/processed", "*.csv", "paul_test", "paul_stats_test", "First" },
			{ "/tmp/monitor01", "/tmp/processed", "*.csv", "paul_test", "paul_stats_test", "Second" }
								};

pid_t   pids[NUM_CHLD];
unsigned int master = 0;
int proc_id;

int fork_children(int nchld)
{
	(void)nchld;	// reserved for future use
	int pipefd[2];

	fprintf(stderr, "Master active: %lu\n", getpid());

	for(int i=0; i<NUM_CHLD; i++){
		if(pipe(pipefd) == -1){
			perror("pipe");
			return -1;
		}
		pids[i] = fork();
		if(pids[i] == -1){
			perror("fork: fork_children");
			return(-1);
		}
		if(pids[i] == 0){
			/* child */
			master = 0;
			close(pipefd[1]);
			read(pipefd[0], &proc_id, sizeof(int));
//			fprintf(stderr, "Child %d (%d) has %s %s %s\n", proc_id, getpid(), params[proc_id].src, params[proc_id].dest, params[proc_id].desc);
			close(pipefd[1]);
			return 0;
		}else{
			/* parent */
			master = 1;
//			fprintf(stderr, "Parent %d has child %d\n", getpid(), pids[i]);
			close(pipefd[0]);
			write(pipefd[1], &i, sizeof(int));
			close(pipefd[1]);
		}
	}

/*	if(master){
		for(int i=0; i<NUM_CHLD; i++){
			wait(NULL);
			fprintf(stderr, "Parent waits %d\n", pids[i]);
		}
	}
*/
	return 0;
}


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

	signal(SIGCHLD, child_handler);
	signal(SIGUSR1, child_handler);
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
//	umask(0113);
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
//	freopen("/dev/null", "w", stderr);

#ifdef WRITE_PIDFILE
	/* Assumes writeable PIDDIR */
	int fd = open(PIDDIR "/" PIDFILE,
				  O_CREAT | O_WRONLY | O_TRUNC,
				  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(fd == -1){
		perror("Unable to write pid: " PIDFILE);
	}else{
		dprintf(fd, "%ld", pid);
		close(fd);
	}
#endif
	kill(ppid, SIGUSR1);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	return 0;
}
