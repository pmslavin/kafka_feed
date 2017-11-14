/*
	Daemon, Syslog and Signals ops
*/
#define _POSIX_C_SOURCE 200809L	// dprintf, kill
#define _GNU_SOURCE				// fopencookie
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
#include <sys/prctl.h>
#include <syslog.h>

#include "daemon.h"
#include "kafkaops.h"
#include "utils.h"
#include "thread.h"

#define WRITE_PIDFILE

#define RUN_AS_USER "nobody"
#define PIDDIR		"/var/run/monitor"
#define PIDFILE		"monitor.pid"

#define NUM_CHLD 2

chld_params_t params[NUM_CHLD] = {
			{ "/tmp/monitor00", "/tmp/processed", "*.csv", "paul_test", "paul_stats_test", "CDR Type 1 feed" },
			{ "/tmp/monitor01", "/tmp/processed", "*.csv", "paul_test", "paul_stats_test", "CDR Type 2 feed"}
								};

pid_t   pids[NUM_CHLD];
unsigned int master = 0;
int proc_id = -1;
typedef void (*sighandler_t)(int);


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
			if(strlen(params[proc_id].desc) > 0)
				prctl(PR_SET_NAME, params[proc_id].desc);
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


/*  Forks child, after which parent exits on signal from child.
 *  Child is then reparented to init.
 */
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
		/* This never happens as child_handler exits */
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


static size_t writer(void *cookie, char const *data, size_t wlen)
{
	(void)cookie;	// unused
	while(*data == ' ')
		++data, --wlen;

	syslog(LOG_INFO, "%.*s", wlen, data);
	return wlen;
}


static size_t noop(void)
{
	return 0;
}


static cookie_io_functions_t iofuncs = {
		(cookie_read_function_t *)noop,
		(cookie_write_function_t *)writer,
		(cookie_seek_function_t *)noop,
		(cookie_close_function_t *)noop
};


void filestr_to_syslog(FILE **fp)
{
	setvbuf(*fp = fopencookie(NULL, "w", iofuncs), NULL, _IOLBF, 0);
}


int init_logger(void)
{
	filestr_to_syslog(&stderr);
	setlogmask(LOG_UPTO(LOG_DEBUG));
	if(proc_id >= 0)
		openlog(params[proc_id].desc, LOG_PID, LOG_LOCAL1);
	else
		openlog("[cdr_monitor]", LOG_PID, LOG_LOCAL1);
	syslog(LOG_NOTICE, "Logger initialised");

	return 0;
}


void sigchld_handler(int sig)
{
	(void)sig;	// unused
	int status;
	pid_t pid = waitpid(-1, &status, WNOHANG);
	if(pid == -1 || pid == 0)
		return;

	int idx;
	for(idx=0; idx < NUM_CHLD; idx++){
		if(pids[idx] == pid)
			break;
	}

	if(WIFEXITED(status)){
		/* handle child exit */
	}else if(WIFSIGNALED(status)){
		int signo = WTERMSIG(status);
		/* handle child signaled */
		fprintf(stderr, "Master catches child (%ld) exit with signal %d\n", pids[idx], signo);
	}
}


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
