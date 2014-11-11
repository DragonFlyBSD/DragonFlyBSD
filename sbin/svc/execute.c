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

int
execute_init(command_t *cmd)
{
        char buf[32];
        pid_t pid;
	pid_t stoppingpid = -1;
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

	if (pipe(fds) < 0) {
		fprintf(cmd->fp, "Unable to create pipe: %s\n",
			strerror(errno));
		return 1;
	}
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
	if (rc) {
		close(fds[0]);
		close(fds[1]);
		return rc;
	}

	/*
	 * Detach the service
	 */
        if ((pid = fork()) != 0) {
                /*
                 * Parent
                 */
		close(fds[1]);
                if (pid < 0) {
                        fprintf(cmd->fp, "fork failed: %s\n", strerror(errno));
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
	 * Service demon is now running.
	 *
	 * Detach from terminal, setup logfile.
	 */
	close(fds[0]);
	fclose(cmd->fp);
	if (cmd->logfile)
		cmd->fp = fopen(cmd->logfile, "a");
	else
		cmd->fp = fdopen(dup(xfd), "a");
	if (xfd != 0) {
		dup2(xfd, 0);
		close(xfd);
	}
	dup2(fileno(cmd->fp), 1);
	dup2(fileno(cmd->fp), 2);
	if ((xfd = open("/dev/tty", O_RDWR)) >= 0) {
		ioctl(xfd, TIOCNOTTY, 0);	/* no controlling tty */
		close(xfd);
	}
	setsid();				/* new session */

	/*
	 * Signal parent that we are completely detached now.
	 */
	c = 1;
	write(fds[1], &c, 1);
	close(fds[1]);
	InitCmd = cmd;

	/*
	 * Start accept thread for unix domain listen socket.
	 * Start service
	 */
	remote_listener(cmd, lfd);
	pthread_mutex_lock(&serial_mtx);
	execute_start(cmd);

	/*
	 * Become the reaper for all children recursively.
	 */
	if (procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL) < 0) {
		fprintf(cmd->fp, "svc is unable to become the "
				 "reaper for its children\n");
		fflush(cmd->fp);
	}

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
					RunState = RS_STOPPED;
					LastStop = ts.tv_sec;
				} /* else still considered normal run state */
			} else {
				/* reap random disconnected child */
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

		/*
		 * If stoppingpid changes we have to reset the TERM->KILL
		 * timer.
		 */
		if (usepid < 0) {
			if (RunState != RS_STOPPED) {
				RunState = RS_STOPPED;
				LastStop = ts.tv_sec;
			}
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
			if (usepid < 0) {
				RunState = RS_STOPPED;
				LastStop = ts.tv_sec;
			}
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
				exit(0);
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
	pthread_mutex_unlock(&serial_mtx);
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
		closefrom(3);
		execvp(InitCmd->ext_av[0], InitCmd->ext_av);
		exit(99);
	}
	if (DirectPid >= 0) {
		RunState = RS_STARTED;
		LastStart = ts.tv_sec;
	} else {
		RunState = RS_STOPPED;
		LastStop = ts.tv_sec;
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

	if (DirectPid >= 0) {
		kill(DirectPid, SIGTERM);
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
	}
	fprintf(cmd->fp, "svc %s: Stopping and Exiting\n", cmd->label);
	execute_stop(cmd);

	exit(0);
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
		if (InitCmd && InitCmd->manual_stop)
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
execute_log(command_t *cmd __unused)
{
	return 0;
}

int
execute_logfile(command_t *cmd)
{
	char *logfile;
	int fd;
	int rc;

	logfile = cmd->logfile;
	if (cmd->ext_av && cmd->ext_av[0])
		logfile = cmd->ext_av[0];
	if (logfile == NULL && InitCmd)
		logfile = InitCmd->logfile;

	rc = 0;
	if (InitCmd && logfile) {
		if (InitCmd->logfile &&
		    strcmp(InitCmd->logfile, logfile) == 0) {
			fprintf(cmd->fp, "svc %s: Reopen logfile %s\n",
				cmd->label, logfile);
		} else {
			fprintf(cmd->fp, "svc %s: Change logfile to %s\n",
				cmd->label, logfile);
		}
		fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND, 0640);
		if (fd >= 0) {
			fflush(stdout);
			fflush(stderr);
			dup2(fd, fileno(stdout));
			dup2(fd, fileno(stderr));
			sreplace(&InitCmd->logfile, logfile);
			close(fd);
		} else {
			fprintf(cmd->fp,
				"svc %s: Unable to open/create \"%s\": %s\n",
				cmd->label, logfile, strerror(errno));
			rc = 1;
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
