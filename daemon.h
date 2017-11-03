#ifndef __DAEMON_H__
#define __DAEMON_H__
/*	Tools for daemon & signal handling */

typedef struct chld_params{
	char	*src;
	char	*dest;
	char 	*filter;
	char 	*topic;
	char	*stats_topic;
	char 	*desc;
}chld_params_t;

extern chld_params_t params[];
extern pid_t pids[];
extern unsigned int master;
extern int proc_id;

int daemonize(void);
int fork_children(int);
int init_logger(void);
void sigchld_handler(int);
void sighup_reload(int);
void sigint_tidy(int);

#endif
