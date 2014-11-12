/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Handle remote listen/connect and parsing operations.
 */

#include "svc.h"

pthread_mutex_t serial_mtx;
time_t LastStart;	/* uptime */
time_t LastStop;	/* uptime */
pid_t DirectPid = -1;
runstate_t RunState = RS_STOPPED;
command_t *InitCmd;
int RestartCounter;

static void *logger_thread(void *arg);
static void setstate_stopped(command_t *cmd, struct timespec *ts);
static int setup_gid(command_t *cmd);
static int setup_uid(command_t *cmd);
static int setup_jail(command_t *cmd);
static int setup_chroot(command_t *cmd);
static int setup_devfs(command_t *cmd, const char *dir, int domount);
static int escapewrite(FILE *fp, char *buf, int n, int *statep);

int
execute_init(command_t *cmd)
{
        char buf[32];
        pid_t pid;
	pid_t stoppingpid = -1;
	pthread_t logtd;
	time_t nextstop = 0;
	int lfd;	/* unix domain listen socket */
	int pfd;	/* pid file */
	int xfd;
	int rc;
	int fds[2];
	char c;

	if (cmd->label == NULL || cmd->ext_ac == 0) {
		fprintf(cmd->fp, "init requires a label and command\n");
		return 1;
	}
	fprintf(cmd->fp, "initializing new service: %s\n", cmd->label);

	if ((xfd = open("/dev/null", O_RDWR)) < 0) {
		fprintf(cmd->fp, "Unable to open /dev/null: %s\n",
			strerror(errno));
		return 1;
	}

	/*
	 * Setup pidfile and unix domain listen socket and lock the
	 * pidfile.
	 */
	rc = setup_pid_and_socket(cmd, &lfd, &pfd);
	if (rc)
		return rc;

	/*
	 * Detach the service
	 */
	if (cmd->foreground) {
		/*
		 * Stay in foreground.
		 */
		fds[0] = -1;
		fds[1] = -1;
		pid = 0;
	} else {
		if (pipe(fds) < 0) {
			fprintf(cmd->fp, "Unable to create pipe: %s\n",
				strerror(errno));
			close(lfd);
			close(pfd);
			remove_pid_and_socket(cmd, cmd->label);
			return 1;
		}
		pid = fork();
	}

	if (pid != 0) {
                /*
                 * Parent
                 */
		close(fds[1]);
                if (pid < 0) {
                        fprintf(cmd->fp, "fork failed: %s\n", strerror(errno));
			close(lfd);
			close(pfd);
			close(fds[0]);
			close(fds[1]);
			remove_pid_and_socket(cmd, cmd->label);
			return 1;
                } else {
			/*
			 * Fill-in pfd before returning.
			 */
			snprintf(buf, sizeof(buf), "%d\n", (int)pid);
			write(pfd, buf, strlen(buf));
		}
		close(lfd);
		close(pfd);

		/*
		 * Wait for child to completely detach from the tty
		 * before returning.
		 */
		read(fds[0], &c, 1);
		close(fds[0]);

		return 0;
        }

	/*
	 * Forked child is now the service demon.
	 *
	 * Detach from terminal, scrap tty, set process title.
	 */
	if (cmd->proctitle) {
		setproctitle("%s - %s", cmd->label, cmd->proctitle);
	} else {
		setproctitle("%s", cmd->label);
	}
	if (cmd->mountdev) {
		if (cmd->jaildir)
			setup_devfs(cmd, cmd->jaildir, 1);
		else if (cmd->rootdir)
			setup_devfs(cmd, cmd->rootdir, 1);
	}

	if (cmd->foreground == 0) {
		close(fds[0]);
		fds[0] = -1;
	}

	if (xfd != 0)				/* scrap tty inputs */
		dup2(xfd, 0);
	if (cmd->foreground == 0) {
		int tfd;

		if (xfd != 1)			/* scrap tty outputs */
			dup2(xfd, 1);
		if (xfd != 2)
			dup2(xfd, 2);

		if ((tfd = open("/dev/tty", O_RDWR)) >= 0) {
			ioctl(tfd, TIOCNOTTY, 0);	/* no controlling tty */
			close(tfd);
		}
		setsid();				/* new session */
	}

	/*
	 * Setup log file.  The log file must not use descriptors 0, 1, or 2.
	 */
	if (cmd->logfile && strcmp(cmd->logfile, "/dev/null") == 0)
		cmd->logfd = -1;
	else if (cmd->logfile)
		cmd->logfd = open(cmd->logfile, O_WRONLY|O_CREAT|O_APPEND, 0640);
	else if (cmd->foreground)
		cmd->logfd = dup(1);
	else
		cmd->logfd = -1;

	/*
	 * Signal parent that we are completely detached now.
	 */
	c = 1;
	if (cmd->foreground == 0) {
		write(fds[1], &c, 1);
		close(fds[1]);
		fds[1] = -1;
	}
	InitCmd = cmd;

	/*
	 * Setup log pipe.  The logger thread copies the pipe to a buffer
	 * for the 'log' directive and also writes it to logfd.
	 */
	pipe(cmd->logfds);
	if (cmd->fp != stdout)
		fclose(cmd->fp);
	cmd->fp = fdopen(cmd->logfds[1], "w");

	if (xfd > 2) {
		close(xfd);
		xfd = -1;
	}

	pthread_cond_init(&cmd->logcond, NULL);

	/*
	 * Start accept thread for unix domain listen socket.
	 */
	pthread_mutex_lock(&serial_mtx);
	pthread_create(&logtd, NULL, logger_thread, cmd);
	remote_listener(cmd, lfd);

	/*
	 * Become the reaper for all children recursively.
	 */
	if (procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL) < 0) {
		fprintf(cmd->fp, "svc is unable to become the "
				 "reaper for its children\n");
		fflush(cmd->fp);
	}

	/*
	 * Initial service start
	 */
	execute_start(cmd);

	/*
	 * Main loop is the reaper
	 */
	for (;;) {
		union reaper_info info;
		struct timespec ts;
		int status;
		int dt;
		pid_t usepid;

		/*
		 * If we are running just block doing normal reaping,
		 * if we are stopping we have to poll for reaping while
		 * we handle stopping.
		 */
		fflush(cmd->fp);
		if (RunState == RS_STARTED) {
			pthread_mutex_unlock(&serial_mtx);
			pid = wait3(&status, 0, NULL);
			pthread_mutex_lock(&serial_mtx);
		} else {
			pid = wait3(&status, WNOHANG, NULL);
		}
		clock_gettime(CLOCK_MONOTONIC_FAST, &ts);

		if (pid > 0) {
			if (pid == DirectPid) {
				fprintf(cmd->fp,
					"svc %s: lost direct child %d\n",
					cmd->label, pid);
				fflush(cmd->fp);
				DirectPid = -1;
				if (cmd->restart_some) {
					setstate_stopped(cmd, &ts);
				} /* else still considered normal run state */
			} else if (cmd->debug) {
				/*
				 * Reap random disconnected child, but don't
				 * spew to the log unless debugging is
				 * enabled.
				 */
				fprintf(cmd->fp,
					"svc %s: reap indirect child %d\n",
					cmd->label,
					(int)pid);
			}
		}

		/*
		 * Calculate the pid to potentially act on and/or
		 * determine if any children still exist.
		 */
		if (DirectPid >= 0) {
			usepid = DirectPid;
		} else if (procctl(P_PID, getpid(),
				   PROC_REAP_STATUS, &info) == 0) {
			usepid = info.status.pid_head;
		} else {
			usepid = -1;
		}
		if (cmd->debug) {
			fprintf(stderr, "svc %s: usepid %d\n",
				cmd->label, usepid);
			fflush(stderr);
		}

		/*
		 * If stoppingpid changes we have to reset the TERM->KILL
		 * timer.
		 */
		if (usepid < 0) {
			setstate_stopped(cmd, &ts);
		} else if (stoppingpid != usepid &&
			   (RunState == RS_STOPPING2 ||
			    RunState == RS_STOPPING3)) {
			RunState = RS_STOPPING1;
		}
		stoppingpid = usepid;

		/*
		 * State machine
		 */
		switch(RunState) {
		case RS_STARTED:
			if (usepid < 0)
				setstate_stopped(cmd, &ts);
			break;
		case RS_STOPPED:
			dt = (int)(ts.tv_sec - LastStop);

			if (cmd->exit_mode) {
				/*
				 * Service demon was told to exit on service
				 * stop (-x passed to init).
				 */
				fprintf(cmd->fp,
					"svc %s: service demon exiting\n",
					cmd->label);
				remove_pid_and_socket(cmd, cmd->label);
				goto exitloop;
			} else if (cmd->manual_stop) {
				/*
				 * Service demon was told to stop via
				 * commanded (not automatic) action.  We
				 * do not auto-restart the service in
				 * this situation.
				 */
				pthread_mutex_unlock(&serial_mtx);
				if (dt < 0 || dt > 60)
					sleep(60);
				else
					sleep(1);
				pthread_mutex_lock(&serial_mtx);
			} else if (cmd->restart_some || cmd->restart_all) {
				/*
				 * Handle automatic restarts
				 */
				if (dt > cmd->restart_timo) {
					execute_start(cmd);
				} else {
					pthread_mutex_unlock(&serial_mtx);
					sleep(1);
					pthread_mutex_lock(&serial_mtx);
				}
			} else {
				/*
				 * No automatic restart was configured,
				 * wait for commanded action.
				 */
				pthread_mutex_unlock(&serial_mtx);
				if (dt < 0 || dt > 60)
					sleep(60);
				else
					sleep(1);
				pthread_mutex_lock(&serial_mtx);
			}
			break;
		case RS_STOPPING1:
			/*
			 * Reset TERM->KILL timer
			 */
			nextstop = ts.tv_sec;
			RunState = RS_STOPPING2;
			/* fall through */
		case RS_STOPPING2:
			if (cmd->termkill_timo == 0) {
				nextstop = ts.tv_sec - 1;
			} else {
				kill(stoppingpid, SIGTERM);
				fprintf(cmd->fp, "svc %s: sigterm %d\n",
					cmd->label, stoppingpid);
				sleep(1);
			}
			RunState = RS_STOPPING3;
			/* fall through */
		case RS_STOPPING3:
			dt = (int)(ts.tv_sec - nextstop);
			if (dt > cmd->termkill_timo) {
				fprintf(cmd->fp, "svc %s: sigkill %d\n",
					cmd->label, stoppingpid);
				kill(stoppingpid, SIGKILL);
			}
			sleep(1);
			break;
		}
	}
exitloop:
	pthread_mutex_unlock(&serial_mtx);
	if (cmd->mountdev) {
		if (cmd->jaildir)
			setup_devfs(cmd, cmd->jaildir, 0);
		else if (cmd->rootdir)
			setup_devfs(cmd, cmd->rootdir, 0);
	}
	exit(0);
	/* does not return */
}

int
execute_start(command_t *cmd)
{
	struct timespec ts;
	int maxwait = 60;

	while (RunState == RS_STOPPING1 ||
	       RunState == RS_STOPPING2 ||
	       RunState == RS_STOPPING3) {
		fprintf(cmd->fp,
			"svc %s: Waiting for previous action to complete\n",
			cmd->label);
		fflush(cmd->fp);
		pthread_mutex_unlock(&serial_mtx);
		sleep(1);
		pthread_mutex_lock(&serial_mtx);
		if (--maxwait == 0) {
			fprintf(cmd->fp,
				"svc %s: Giving up waiting for action\n",
				cmd->label);
			fflush(cmd->fp);
			break;
		}
	}
	if (RunState == RS_STARTED) {
		fprintf(cmd->fp, "svc %s: Already started pid %d\n",
			cmd->label, DirectPid);
		fflush(cmd->fp);
		return 0;
	}

	clock_gettime(CLOCK_MONOTONIC_FAST, &ts);
	if ((DirectPid = fork()) == 0) {
		fflush(InitCmd->fp);
						/* leave stdin /dev/null */
		dup2(fileno(InitCmd->fp), 1);	/* setup stdout */
		dup2(fileno(InitCmd->fp), 2);	/* setup stderr */
		closefrom(3);

		if (cmd->jaildir)		/* jail or chroot */
			setup_jail(cmd);
		else if (cmd->rootdir)
			setup_chroot(cmd);

		setup_gid(cmd);
		setup_uid(cmd);
		execvp(InitCmd->ext_av[0], InitCmd->ext_av);
		exit(99);
	}
	if (DirectPid >= 0) {
		RunState = RS_STARTED;
		LastStart = ts.tv_sec;
	} else {
		setstate_stopped(InitCmd, &ts);
	}
	InitCmd->manual_stop = 0;
	fprintf(cmd->fp, "svc %s: Starting pid %d\n", cmd->label, DirectPid);
	fflush(cmd->fp);

	return 0;
}

int
execute_restart(command_t *cmd)
{
	int rc;

	rc = execute_stop(cmd) + execute_start(cmd);
	return rc;
}

int
execute_stop(command_t *cmd)
{
	union reaper_info info;
	struct timespec ts;
	int save_restart_some;
	int save_restart_all;
	int maxwait = 60;

	save_restart_some = InitCmd->restart_some;
	save_restart_all = InitCmd->restart_all;
	if (cmd->commanded)
		InitCmd->manual_stop = 1;
	if (cmd->commanded && (cmd->restart_some || cmd->restart_all)) {
		InitCmd->restart_some = cmd->restart_some;
		InitCmd->restart_all = cmd->restart_all;
	}
	fprintf(cmd->fp, "svc %s: Stopping\n", cmd->label);
	fflush(cmd->fp);

	/*
	 * Start the kill chain going so the master loop's wait3 wakes up.
	 */
	if (DirectPid >= 0) {
		kill(DirectPid, SIGTERM);
	} else {
		if (procctl(P_PID, getpid(), PROC_REAP_STATUS, &info) == 0 &&
		    info.status.pid_head > 0) {
			kill(info.status.pid_head, SIGTERM);
		}
	}

	clock_gettime(CLOCK_MONOTONIC_FAST, &ts);
	LastStop = ts.tv_sec;
	RunState = RS_STOPPING1;

	/*
	 * If commanded (verses automatic), we are running remote in our
	 * own thread and we need to wait for the action to complete.
	 */
	if (cmd->commanded) {
		while (RunState == RS_STOPPING1 ||
		       RunState == RS_STOPPING2 ||
		       RunState == RS_STOPPING3) {
			fprintf(cmd->fp,
				"svc %s: Waiting for service to stop\n",
				cmd->label);
			fflush(cmd->fp);
			pthread_mutex_unlock(&serial_mtx);
			sleep(1);
			pthread_mutex_lock(&serial_mtx);
			if (--maxwait == 0) {
				fprintf(cmd->fp,
					"svc %s: Giving up waiting for stop\n",
					cmd->label);
				fflush(cmd->fp);
				break;
			}
		}
		if (cmd->restart_some || cmd->restart_all) {
			InitCmd->restart_some = save_restart_some;
			InitCmd->restart_all = save_restart_all;
		}
	}

	return 0;
}

int
execute_exit(command_t *cmd)
{
	if (cmd->commanded) {
		InitCmd->restart_some = 0;
		InitCmd->restart_all = 1;	/* kill all children */
		InitCmd->exit_mode = 1;		/* exit after stop */
	} else {
		cmd->exit_mode = 1;
	}
	fprintf(cmd->fp, "svc %s: Stopping and Exiting\n", cmd->label);
	execute_stop(cmd);

	return 0;
}

int
execute_list(command_t *cmd)
{
	fprintf(cmd->fp, "%-16s\n", cmd->label);

	return 0;
}

int
execute_status(command_t *cmd)
{
	const char *state;

	switch(RunState) {
	case RS_STOPPED:
		if (InitCmd && InitCmd->exit_mode)
			state = "stopped (exiting)";
		else if (InitCmd && InitCmd->manual_stop)
			state = "stopped (manual)";
		else
			state = "stopped";
		break;
	case RS_STARTED:
		state = "running";
		break;
	case RS_STOPPING1:
	case RS_STOPPING2:
	case RS_STOPPING3:
		state = "killing";
		break;
	default:
		state = "unknown";
		break;
	}

	fprintf(cmd->fp, "%-16s %s\n", cmd->label, state);

	return 0;
}

int
execute_log(command_t *cmd)
{
	int lbsize = (int)sizeof(cmd->logbuf);
	int lbmask = lbsize - 1;
	int windex;
	int n;
	int lastnl;
	int dotstate;
	char buf[LOGCHUNK];

	assert(InitCmd);

	/*
	 * mode 0 - Dump everything then exit
	 * mode 1 - Dump everything then block/loop
	 * mode 2 - Skeep to end then block/loop
	 */
	if (cmd->tail_mode == 2)
		windex = InitCmd->logwindex;
	else
		windex = InitCmd->logwindex - InitCmd->logcount;
	lastnl = 1;
	dotstate = 0;	/* 0=start-of-line 1=middle-of-line 2=dot */

	for (;;) {
		/*
		 * Calculate the amount of data we missed and determine
		 * if some data was lost.
		 */
		n = InitCmd->logwindex - windex;
		if (n < 0 || n > InitCmd->logcount) {
			windex = InitCmd->logwindex - InitCmd->logcount;
			pthread_mutex_unlock(&serial_mtx);
			fprintf(cmd->fp, "\n(LOG DATA LOST)\n");
			pthread_mutex_lock(&serial_mtx);
			continue;
		}

		/*
		 * Circular buffer and copy size limitations.  If no
		 * data ready, wait for some.
		 */
		if (n > lbsize - (windex & lbmask))
			n = lbsize - (windex & lbmask);
		if (n > LOGCHUNK)
			n = LOGCHUNK;
		if (n == 0) {
			if (cmd->tail_mode == 0)
				break;
			pthread_cond_wait(&InitCmd->logcond, &serial_mtx);
			continue;
		}
		bcopy(InitCmd->logbuf + (windex & lbmask), buf, n);

		/*
		 * Dump log output, escape any '.' on a line by itself.
		 */
		pthread_mutex_unlock(&serial_mtx);
		n = escapewrite(cmd->fp, buf, n, &dotstate);
		fflush(cmd->fp);
		if (n > 0)
			lastnl = (buf[n-1] == '\n');
		pthread_mutex_lock(&serial_mtx);

		if (n < 0)
			break;
		windex += n;
	}
	if (lastnl == 0) {
		pthread_mutex_unlock(&serial_mtx);
		fprintf(cmd->fp, "\n");
		pthread_mutex_lock(&serial_mtx);
	}
	return 0;
}

/*
 * Change or reopen logfile.
 */
int
execute_logfile(command_t *cmd)
{
	char *logfile;
	int fd;
	int rc;

	assert(InitCmd);

	logfile = cmd->logfile;
	if (cmd->ext_av && cmd->ext_av[0])
		logfile = cmd->ext_av[0];
	if (logfile == NULL)
		logfile = InitCmd->logfile;

	rc = 0;
	if (logfile) {
		if (InitCmd->logfile &&
		    strcmp(InitCmd->logfile, logfile) == 0) {
			fprintf(cmd->fp, "svc %s: Reopen logfile %s\n",
				cmd->label, logfile);
		} else {
			fprintf(cmd->fp, "svc %s: Change logfile to %s\n",
				cmd->label, logfile);
		}
		if (InitCmd->logfd >= 0) {
			close(InitCmd->logfd);
			InitCmd->logfd = -1;
		}
		if (strcmp(logfile, "/dev/null") == 0) {
			sreplace(&InitCmd->logfile, logfile);
		} else {
			fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND, 0640);
			if (fd >= 0) {
				InitCmd->logfd = fd;
				sreplace(&InitCmd->logfile, logfile);
			} else {
				fprintf(cmd->fp,
					"svc %s: Unable to open/create "
					"\"%s\": %s\n",
					cmd->label,
					logfile, strerror(errno));
				rc = 1;
			}
		}
	}
	return rc;
}

int
execute_help(command_t *cmd)
{
	fprintf(cmd->fp,
		"svc [options] directive [label [additional_args]]\n"
		"\n"
		"Directives: init start stop stopall restart exit\n"
		"            kill list status log logf tailf logfile\n"
		"            help\n"
	);
	return 0;
}

static
void *
logger_thread(void *arg)
{
	command_t *cmd = arg;
	int lbsize = (int)sizeof(cmd->logbuf);
	int lbmask = lbsize - 1;
	int windex;
	int n;

	pthread_detach(pthread_self());
	pthread_mutex_lock(&serial_mtx);
	for (;;) {
		/*
		 * slip circular buffer to make room for new data.
		 */
		n = cmd->logcount - (lbsize - LOGCHUNK);
		if (n > 0) {
			cmd->logcount -= n;
			cmd->logwindex += n;
		}
		windex = cmd->logwindex & lbmask;
		n = lbsize - windex;
		if (n > LOGCHUNK)
			n = LOGCHUNK;
		pthread_mutex_unlock(&serial_mtx);
		n = read(cmd->logfds[0], cmd->logbuf + windex, n);
		pthread_mutex_lock(&serial_mtx);
		if (n > 0) {
			if (cmd->logfd >= 0)
				write(cmd->logfd, cmd->logbuf + windex, n);
			cmd->logcount += n;
			cmd->logwindex += n;
			pthread_cond_signal(&cmd->logcond);
		}
		if (n == 0 || (n < 0 && errno != EINTR))
			break;
	}
	pthread_mutex_unlock(&serial_mtx);
	return NULL;
}

/*
 * Put us in the STOPPED state if we are not already there, and
 * handle post-stop options (aka sync).
 */
static
void
setstate_stopped(command_t *cmd, struct timespec *ts)
{
	if (RunState != RS_STOPPED) {
		RunState = RS_STOPPED;
		LastStop = ts->tv_sec;
		if (cmd->sync_mode)	/* support -s option */
			sync();
	}
}

static
int
setup_gid(command_t *cmd)
{
	int i;

	if (cmd->gid_mode &&
	    setgid(cmd->grent.gr_gid) < 0) {
		fprintf(cmd->fp, "unable to setgid to \"%s\": %s\n",
			cmd->grent.gr_name, strerror(errno));
		return 1;
	}

	/*
	 * -G overrides all group ids.
	 */
	if (cmd->ngroups) {
		if (setgroups(cmd->ngroups, cmd->groups) < 0) {
			fprintf(cmd->fp, "unable to setgroups to (");
			for (i = 0; i < cmd->ngroups; ++i) {
				if (i)
					fprintf(cmd->fp, ", ");
				fprintf(cmd->fp, "%d", cmd->groups[i]);
			}
			fprintf(cmd->fp, "): %s\n", strerror(errno));
			return 1;
		}
	}
	return 0;
}

static
int
setup_uid(command_t *cmd)
{
	fprintf(stderr, "UIDMODE %d %d\n", cmd->uid_mode, cmd->pwent.pw_uid);
	if (cmd->uid_mode &&
	    cmd->gid_mode == 0 &&
	    cmd->ngroups == 0 &&
	    setgid(cmd->pwent.pw_gid) < 0) {
		fprintf(cmd->fp, "unable to setgid for user \"%s\": %s\n",
			cmd->pwent.pw_name,
			strerror(errno));
		return 1;
	}
	if (cmd->uid_mode &&
	    setuid(cmd->pwent.pw_uid) < 0) {
		fprintf(cmd->fp, "unable to setuid for user \"%s\": %s\n",
			cmd->pwent.pw_name,
			strerror(errno));
		return 1;
	}
	return 0;
}

static
int
setup_jail(command_t *cmd)
{
	struct jail info;
	char hostbuf[256];

	if (gethostname(hostbuf, sizeof(hostbuf) - 1) < 0) {
		fprintf(cmd->fp, "gethostname() failed: %s\n", strerror(errno));
		return 1;
	}
	/* make sure it is zero terminated */
	hostbuf[sizeof(hostbuf) -1] = 0;

	bzero(&info, sizeof(info));
	info.version = 1;
	info.path = cmd->jaildir;
	info.hostname = hostbuf;
	/* info.n_ips, sockaddr_storage ips[] */

	if (jail(&info) < 0) {
		fprintf(cmd->fp, "unable to create jail \"%s\": %s\n",
			cmd->rootdir,
			strerror(errno));
		return 1;
	}
	return 0;
}

static
int
setup_chroot(command_t *cmd)
{
	if (chroot(cmd->rootdir) < 0) {
		fprintf(cmd->fp, "unable to chroot to \"%s\": %s\n",
			cmd->rootdir,
			strerror(errno));
		return 1;
	}
	return 0;
}

static
int
setup_devfs(command_t *cmd, const char *dir, int domount)
{
	struct devfs_mount_info info;
	struct statfs fs;
	int rc = 0;
	char *path;

	bzero(&info, sizeof(info));
	info.flags = 0;
	asprintf(&path, "%s/dev", dir);

	if (domount) {
		if (statfs(path, &fs) == 0 &&
		    strcmp(fs.f_fstypename, "devfs") == 0) {
			fprintf(cmd->fp, "devfs already mounted\n");
		} else
		if (mount("devfs", path, MNT_NOEXEC|MNT_NOSUID, &info) < 0) {
			fprintf(cmd->fp, "cannot mount devfs on %s: %s\n",
				path, strerror(errno));
			rc = 1;
		}
	} else {
		if (statfs(path, &fs) < 0 ||
		    strcmp(fs.f_fstypename, "devfs") != 0) {
			fprintf(cmd->fp, "devfs already unmounted\n");
		} else
		if (unmount(path, 0) < 0) {
			fprintf(cmd->fp, "cannot unmount devfs from %s: %s\n",
				path, strerror(errno));
			rc = 1;
		}
	}
	free(path);
	return rc;
}

/*
 * Escape writes.  A '.' on a line by itself must be escaped to '..'.
 */
static
int
escapewrite(FILE *fp, char *buf, int n, int *statep)
{
	int b;
	int i;
	int r;
	char c;

	b = 0;
	r = 0;
	while (i < n) {
		for (i = b; i < n; ++i) {
			c = buf[i];

			switch(*statep) {
			case 0:
				/*
				 * beginning of line
				 */
				if (c == '.')
					*statep = 2;
				else if (c != '\n')
					*statep = 1;
				break;
			case 1:
				/*
				 * middle of line
				 */
				if (c == '\n')
					*statep = 0;
				break;
			case 2:
				/*
				 * dot was output at beginning of line
				 */
				if (c == '\n')
					*statep = 3;
				else
					*statep = 1;
				break;
			default:
				break;
			}
			if (*statep == 3)	/* flush with escape */
				break;
		}
		if (i != b) {
			n = fwrite(buf, 1, i - b, fp);
			if (n > 0)
				r += n;
			if (n < 0)
				r = -1;
		}
		if (*statep == 3) {		/* added escape */
			n = fwrite(".", 1, 1, fp);
			/* escapes not counted in r */
			*statep = 1;
			if (n < 0)
				r = -1;
		}
		if (r < 0)
			break;
	}
	return r;
}
