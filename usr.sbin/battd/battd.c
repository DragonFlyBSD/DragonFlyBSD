/*
 * Copyright (c) 2003, 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Liam J. Foy <liamfoy@dragonflybsd.org> 
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
 *
 * $DragonFly: src/usr.sbin/battd/battd.c,v 1.12 2008/02/22 04:30:34 swildner Exp $
 *
 * Dedicated to my grandfather Peter Foy. Goodnight... 
 */

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <machine/apm_bios.h>
#include <limits.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define APMUNKNOWN	255 /* Unknown value. */
#define	AC_LINE_IN	1 /* AC Line Status values. */
#define AC_OFFLINE	0
#define SECONDS		60
#define APM_DEVICE	"/dev/apm" /* Default APM Device. */
#define ERR_OUT		1  /* Values for error writing. */
#define SYSLOG_OUT	0
#define DEFAULT_ALERT	10 /* Default alert is 10%. */
#define EXEC_ALL	0
#define EXEC_ONCE	1
#define EXEC_DONE	2

struct battd_conf {
	int	alert_per;	/* Percentage to alert user on */
	int	alert_time; 	/* User can also alert when there is only X amount of minutes left */
	int	alert_status;	/* Alert when battery is either high, low, critical */ 
	const char	*apm_dev; 	/* APM Device */
	const char	*exec_cmd; 	/* Command to execute if desired */	
};

#ifdef DEBUG
static int f_debug;
#endif

static int	check_percent(int);
static int	check_stat(int);
static int	check_time(int);
static int	get_apm_info(struct apm_info *, int, const char *, int);
static void	execute_cmd(const char *, int *);
static void	write_emerg(const char *, int *);
static void	usage(void) __dead2;

static void
usage(void)
{
#ifdef DEBUG
	fprintf(stderr, "usage: battd [-dEhT] [-c seconds] [-e command] [-f device]\n"
			"             [-p percent] [-s status] [-t minutes]\n");
#else
	fprintf(stderr, "usage: battd [-EhT] [-c seconds] [-e command] [-f device]\n"
			"             [-p percent] [-s status] [-t minutes]\n");
#endif
	exit(EXIT_FAILURE);
}

static int
check_percent(int apm_per)
{
	if (apm_per < 0 || apm_per >= APMUNKNOWN || apm_per > 100)
		return(1);

	return(0);
}

static int
check_time(int apm_time)
{
	if (apm_time <= -1)
		return(1);

	return(0);
}

static int
check_stat(int apm_stat)
{
	if (apm_stat > 3 || apm_stat < 0)
		return(1);

	return(0);
}

/* Fetch battery information */
static int
get_apm_info(struct apm_info *ai, int fp_dev, const char *apm_dev, int err_to)
{
	if (ioctl(fp_dev, APMIO_GETINFO, ai) == -1) {
		if (err_to)
			err(1, "ioctl(APMIO_GETINFO) device: %s", apm_dev);
		else
			syslog(LOG_ERR, "ioctl(APMIO_GETINFO) device: %s: %m ",
				apm_dev);

		return(1);
	}

	return(0);
}

/* Execute command. */
static void
execute_cmd(const char *exec_cmd, int *exec_cont)
{
	pid_t pid;
	int status;
		
	if (*exec_cont != EXEC_DONE) {
		if ((pid = fork()) == -1) {
			/* Here fork failed */
#ifdef DEBUG
			if (f_debug)
				warn("fork failed");
			else
#endif
				syslog(LOG_ERR, "fork failed: %m");
		} else if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", exec_cmd, NULL);
			_exit(EXIT_FAILURE);
		} else {
			while (waitpid(pid, &status, 0) != pid)
				;
			if (WEXITSTATUS(status)) {
#ifdef DEBUG
				if (f_debug)
					warnx("child exited with code %d", status);
				else
#endif
					syslog(LOG_ERR, "child exited with code %d", status);
			}
			if (*exec_cont == EXEC_ONCE)
				*exec_cont = EXEC_DONE;
		}
	}
}

/* Write warning. */
static void
write_emerg(const char *eme_msg, int *warn_cont)
{
	if (*warn_cont == 0) {
		openlog("battd", LOG_EMERG, LOG_CONSOLE);
		syslog(LOG_ERR, "%s\n", eme_msg);
		*warn_cont = 1;
	}
}

/* Check given numerical arguments. */
static int
getnum(const char *str)
{
	long val;
	char *ep;

	errno = 0;
	val = strtol(str, &ep, 10);
	if (errno)
		err(1, "strtol failed: %s", str);

	if (str == ep || *ep != '\0')
		errx(1, "invalid value: %s", str);

	if (val > INT_MAX || val < INT_MIN) {
		errno = ERANGE;
		errc(1, errno, "getnum failed:");
	}

	return((int)val);
}

int
main(int argc, char **argv)
{
	struct battd_conf battd_options, *opts;
	struct apm_info ai;
	int fp_device, exec_cont;
	int per_warn_cont, time_warn_cont, stat_warn_cont;
	int check_sec, time_def_alert, def_warn_cont;
	int c, tmp;
	char msg[80];

	opts = &battd_options;

	/*
	 * As default, we sleep for 30 seconds before
	 * we next call get_apm_info and do the rounds.
	 * The lower the value, the more accurate. Very
	 * low values could cause a decrease in system
	 * performance. We recommend about 30 seconds.
	 */

	check_sec = 30;

	exec_cont = EXEC_ALL;
	per_warn_cont = stat_warn_cont = 0;
	time_warn_cont = time_def_alert = def_warn_cont = 0;

	opts->alert_per = DEFAULT_ALERT;
	opts->alert_time = 0;
	opts->alert_status = -1;
	opts->exec_cmd = NULL;
	opts->apm_dev = APM_DEVICE;

	while ((c = getopt(argc, argv, "de:Ep:s:c:f:ht:T")) != -1) {
		switch (c) {
		case 'c':
			/* Parse the check battery interval. */
			check_sec = getnum(optarg);
			if (check_sec <= 0)
				errx(1, "the interval for checking battery "
					"status must be greater than 0.");
			break;
#ifdef DEBUG
		case 'd':
			/* Debug mode. */
			f_debug = 1;
			break;
#endif
		case 'e':
			/* Command to execute. */
			opts->exec_cmd = optarg;
			break;
		case 'E':
			/* Only execute once when any condition has been met. */
			exec_cont = EXEC_ONCE;
			break;
		case 'f':
			/* Don't use /dev/apm use optarg. */
			opts->apm_dev = optarg;
			break;
		case 'h':
			/* Print usage and be done! */
			usage();
			break;
		case 'p':
			/*
			 * Parse percentage to alert on and enable
			 * battd to monitor the battery percentage.
			 * A value of 0 disables battery percentage monitoring.
			 */
			opts->alert_per = getnum(optarg);
			if (opts->alert_per < 0 || opts->alert_per > 99)
				errx(1,	"Battery percentage to alert on must be "
					"between 0 and 99.");
			break;
		case 's':
			/*
			 * Parse status to alert on and enable
			 * battd to monitor the battery status.
			 * We also accept 'high', 'HIGH' or 'HiGh'.
			 */
			if (strcasecmp(optarg, "high") == 0)
				opts->alert_status = 0; /* High */
			else if (strcasecmp(optarg, "low") == 0)
				opts->alert_status = 1; /* Low */
			else if (strcasecmp(optarg, "critical") == 0)
				opts->alert_status = 2; /* Critical (mental) */
			else {
				/* No idea, see what we have. */
				opts->alert_status = getnum(optarg);
				if (opts->alert_status < 0 || opts->alert_status > 2)
					errx(1, "Alert status must be between 0 and 2.");
			}
			break;
		case 't':
			/*
			 * Parse time to alert on and enable
			 * battd to monitor the time percentage.
			 */
			opts->alert_time = getnum(optarg);
			if (opts->alert_time <= 0)
				errx(1, "Alert time must be greater than 0 minutes.");
			break;
		case 'T':
			time_def_alert = 1;
			break;
		default:
			usage();
		}
	}

	if ((fp_device = open(opts->apm_dev, O_RDONLY)) < 0)
		err(1, "open failed: %s", opts->apm_dev);

	/* 
	 * Before we become a daemon, first check whether
	 * the actual function requested is supported. If
	 * not, exit and let the user know.
	 */

	/* Start test */
	get_apm_info(&ai, fp_device, opts->apm_dev, ERR_OUT);

	if (opts->alert_per > 0)
		if (check_percent(ai.ai_batt_life))
			errx(1, "invalid/unknown percentage(%d) returned from %s",
				ai.ai_batt_life, opts->apm_dev);

	if (opts->alert_time || time_def_alert)
		if (check_time(ai.ai_batt_time) && ai.ai_batt_time != -1)
			errx(1, "invalid/unknown time(%d) returned from %s",
				ai.ai_batt_time, opts->apm_dev);

	if (opts->alert_status)
		if (check_stat(ai.ai_batt_stat))
			errx(1, "invalid/unknown status(%d) returned from %s",
				ai.ai_batt_stat, opts->apm_dev);
	/* End test */

#ifdef DEBUG
	if (f_debug == 0) {
#endif
		struct pidfh *pfh = NULL;

		pfh = pidfile_open(NULL, 600, NULL);
		if (daemon(0, 0) == -1)
			err(1, "daemon failed");
		pidfile_write(pfh);
#ifdef DEBUG
	}
#endif

	for (;;) {
		if (get_apm_info(&ai, fp_device, opts->apm_dev,
#ifdef DEBUG
			f_debug ? ERR_OUT : SYSLOG_OUT))
#else
			SYSLOG_OUT))
#endif
			/* Recoverable - sleep for check_sec seconds */
			goto sleepy_time;

		/* If we have power, reset the warning values. */
		if (ai.ai_acline == AC_LINE_IN) {
			per_warn_cont = 0;
			time_warn_cont = 0;
			stat_warn_cont = 0;
			def_warn_cont = 0;
		}

		/*
		 * If the battery has main power (AC lead is plugged in)
		 * we skip and sleep for check_sec seconds.
		 */
		if (ai.ai_acline != AC_LINE_IN) {
			/*
			 * Battery has no mains power. Time to do
			 * our job!
			 */

			/*
			 * Main Processing loop
			 * --------------------
			 * 1. Check battery percentage if enabled.
			 * 2. Check battery time remaining if enabled.
			 * 3. Check battery status if enabled.
			 * 4. Deal with time default alert.
			 */ 

			/* 1. Check battery percentage if enabled */
			if (opts->alert_per) {
				if (check_percent(ai.ai_batt_life)) {
#ifdef DEBUG
					if (f_debug) {
						printf("Invalid percentage (%d) received from %s.\n",
							ai.ai_batt_life, opts->apm_dev);
					} else {
#endif
						syslog(LOG_ERR, "Invalid percentage received from %s.",
							opts->apm_dev);
#ifdef DEBUG
					}
#endif
					continue;
				}

				if (ai.ai_batt_life <= (u_int)opts->alert_per) {
					tmp = (ai.ai_batt_life == (u_int)opts->alert_per);
					snprintf(msg, sizeof(msg), "battery has %s %d%%\n",
						tmp ? "reached" : "fallen below",
						opts->alert_per);
					execute_cmd(opts->exec_cmd, &exec_cont);
					write_emerg(msg, &per_warn_cont);
				}
			}

			/* 2. Check battery time remaining if enabled */
			if (opts->alert_time) {
				if (check_time(ai.ai_batt_time)) {
#ifdef DEBUG
					if (f_debug) {
						printf("Invalid time value (%d) received from %s.\n",
							ai.ai_batt_time, opts->apm_dev);
					} else {
#endif
						syslog(LOG_ERR, "Invalid time value received from %s.",
							opts->apm_dev);
#ifdef DEBUG
					}
#endif
					continue;
				}
	
				if (ai.ai_batt_time <= (opts->alert_time * SECONDS)) {
					int h, m, s;
					char tmp_time[sizeof "tt:tt:tt" + 1];
					h = ai.ai_batt_time;
					s = h % 60;
					h /= 60;
					m = h % 60;
					h /= 60;
					snprintf(tmp_time, sizeof(tmp_time), "%d:%d:%d\n", h, m, s);
					tmp = (ai.ai_batt_time == opts->alert_time);
					snprintf(msg, sizeof(msg), "battery has %s %d(%s) minutes "
						"remaining\n", tmp ? "reached" : "fallen below",
						ai.ai_batt_time / SECONDS, tmp_time);
					execute_cmd(opts->exec_cmd, &exec_cont);
					write_emerg(msg, &time_warn_cont);
				}
			}

			/* 3. Check battery status if enabled */
			if (opts->alert_status != -1) {
				if (check_stat(ai.ai_batt_stat)) {
#ifdef DEBUG
					if (f_debug) {
						printf("Invalid status value (%d) received from %s.\n",
							ai.ai_batt_life, opts->apm_dev);
					} else {
#endif
						syslog(LOG_ERR, "Invalid status value received from %s.",
							opts->apm_dev);
#ifdef DEBUG
					}
#endif
					continue;
				}

				if (ai.ai_batt_stat >= (u_int)opts->alert_status) {
					const char *batt_status[] = {"high", "low", "critical"};

					tmp = (ai.ai_batt_stat == (u_int)opts->alert_status);
					snprintf(msg, sizeof(msg), "battery has %s '%s' status\n",
						tmp ? "reached" : "fallen below",
						batt_status[ai.ai_batt_stat]);
					execute_cmd(opts->exec_cmd, &exec_cont);
					write_emerg(msg, &stat_warn_cont);
				}
			}

			/* 4. Deal with time default alert. */
			if (time_def_alert) {
				if (check_time(ai.ai_batt_time)) {
#ifdef DEBUG
					if (f_debug) {
						printf("Invalid time value (%d) received from %s.\n",
							ai.ai_batt_time, opts->apm_dev);
					} else {
#endif
						syslog(LOG_ERR, "Invalid time value received from %s.",
							opts->apm_dev);
#ifdef DEBUG
					}
#endif
					continue;
				}

				if (ai.ai_batt_time <= DEFAULT_ALERT * SECONDS) {
					snprintf(msg, sizeof(msg), "WARNING! battery has "
						"only roughly %d minutes remaining!\n",
						ai.ai_batt_time / SECONDS);
					write_emerg(msg, &def_warn_cont);
				}
			}

		}
sleepy_time:
		/* Sleep time! Default is 30 seconds */
		sleep(check_sec);
	}
	return(0);
}
