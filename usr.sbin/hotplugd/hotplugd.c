/*	$OpenBSD: hotplugd.c,v 1.11 2009/06/26 01:06:04 kurt Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Devices hot plugging daemon.
 */

#include <sys/types.h>
#include <sys/device.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <devattr.h>
#include "compat.h"

#define _PATH_DEV_HOTPLUG		"/dev/hotplug"
#define _PATH_ETC_HOTPLUG		"/etc/hotplug"
#define _PATH_ETC_HOTPLUG_ATTACH	_PATH_ETC_HOTPLUG "/attach"
#define _PATH_ETC_HOTPLUG_DETACH	_PATH_ETC_HOTPLUG "/detach"
#define _LOG_TAG			"hotplugd"
#define _LOG_FACILITY			LOG_DAEMON
#define _LOG_OPT			(LOG_NDELAY | LOG_PID)

enum obsd_devclass {
	DV_DULL,		/* generic, no special info */
	DV_CPU,			/* CPU (carries resource utilization) */
	DV_DISK,		/* disk drive (label, etc) */
	DV_IFNET,		/* network interface */
	DV_TAPE,		/* tape device */
	DV_TTY			/* serial line interface (???) */
};

extern char *__progname;

volatile sig_atomic_t quit = 0;

void exec_script(const char *, int, const char *);
void sigchild(int);
void sigquit(int);
__dead void usage(void);

int
main(int argc, char *argv[])
{
	int ch, class, ret;
	struct sigaction sact;
	struct udev *udev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_le, *udev_le_first;
	struct udev_monitor *udev_monitor;
	struct udev_device *udev_dev;
	enum obsd_devclass devclass;
	const char *prop;

	while ((ch = getopt(argc, argv, "?")) != -1)
		switch (ch) {
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	udev = udev_new();
	if (udev == NULL)
		err(1, "udev_new");

	bzero(&sact, sizeof(sact));
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
	sact.sa_handler = sigquit;
	sigaction(SIGINT, &sact, NULL);
	sigaction(SIGQUIT, &sact, NULL);
	sigaction(SIGTERM, &sact, NULL);
	sact.sa_handler = SIG_IGN;
	sigaction(SIGHUP, &sact, NULL);
	sact.sa_handler = sigchild;
	sact.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sact, NULL);

	openlog(_LOG_TAG, _LOG_OPT, _LOG_FACILITY);

	if (daemon(0, 0) == -1)
		err(1, "daemon");

	syslog(LOG_INFO, "started");

	udev_enum = udev_enumerate_new(udev);
	if (udev_enum == NULL)
		err(1, "udev_enumerate_new");

	ret = udev_enumerate_scan_devices(udev_enum);
	if (ret != 0)
		err(1, "udev_enumerate_scan_device ret = %d", ret);

	udev_le_first = udev_enumerate_get_list_entry(udev_enum);
	if (udev_le_first == NULL)
		err(1, "udev_enumerate_get_list_entry error");

	udev_list_entry_foreach(udev_le, udev_le_first) {
		udev_dev = udev_list_entry_get_device(udev_le);

		class = atoi(udev_device_get_property_value(udev_dev, "devtype"));
		devclass = ((class == D_TTY) ? DV_TTY : ((class == D_TAPE) ? DV_TAPE : ((class == D_DISK) ? DV_DISK : DV_DULL)));

		syslog(LOG_INFO, "%s attached, class %d",
		    udev_device_get_devnode(udev_dev), devclass);
		exec_script(_PATH_ETC_HOTPLUG_ATTACH, devclass,
		   udev_device_get_devnode(udev_dev));
	}

	udev_enumerate_unref(udev_enum);
	udev_monitor = udev_monitor_new(udev);

	ret = udev_monitor_enable_receiving(udev_monitor);
	if (ret != 0)
		err(1, "udev_monitor_enable_receiving ret = %d", ret);

	while (!quit) {
		if ((udev_dev = udev_monitor_receive_device(udev_monitor)) == NULL) {
			syslog(LOG_ERR, "read: %m");
			exit(1);
		}

		prop = udev_device_get_action(udev_dev);
		class = atoi(udev_device_get_property_value(udev_dev, "devtype"));
		devclass = ((class == D_TTY) ? DV_TTY : ((class == D_TAPE) ? DV_TAPE : ((class == D_DISK) ? DV_DISK : DV_DULL)));

		if (strcmp(prop, "attach") == 0) {
			syslog(LOG_INFO, "%s attached, class %d",
			    udev_device_get_devnode(udev_dev), devclass);
			exec_script(_PATH_ETC_HOTPLUG_ATTACH, devclass,
			    udev_device_get_devnode(udev_dev));
		} else if (strcmp(prop, "detach") == 0) {
			syslog(LOG_INFO, "%s detached, class %d",
			    udev_device_get_devnode(udev_dev), devclass);
			exec_script(_PATH_ETC_HOTPLUG_DETACH, devclass,
			    udev_device_get_devnode(udev_dev));
		} else {
			syslog(LOG_NOTICE, "unknown event (%s)", prop);
		}
	}

	syslog(LOG_INFO, "terminated");

	closelog();

	udev_monitor_unref(udev_monitor);
	udev_unref(udev);

	return (0);
}

void
exec_script(const char *file, int class, const char *name)
{
	char strclass[8];
	pid_t pid;

	snprintf(strclass, sizeof(strclass), "%d", class);

	if (access(file, X_OK | R_OK)) {
		syslog(LOG_ERR, "could not access %s", file);
		return;
	}

	if ((pid = fork()) == -1) {
		syslog(LOG_ERR, "fork: %m");
		return;
	}
	if (pid == 0) {
		/* child process */
		execl(file, basename(file), strclass, name, NULL);
		syslog(LOG_ERR, "execl %s: %m", file);
		_exit(1);
		/* NOTREACHED */
	}
}

/* ARGSUSED */
void
sigchild(int signum __unused)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;
	int saved_errno, status;
	pid_t pid;

	saved_errno = errno;

	sdata.log_tag = _LOG_TAG;
	sdata.log_fac = _LOG_FACILITY;
	sdata.log_stat = _LOG_OPT;

	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == EINTR)
				continue;
			if (errno != ECHILD)
				syslog_r(LOG_ERR, &sdata, "waitpid: %m");
			break;
		}

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0) {
				syslog_r(LOG_NOTICE, &sdata,
				    "child exit status: %d",
				    WEXITSTATUS(status));
			}
		} else {
			syslog_r(LOG_NOTICE, &sdata,
			    "child is terminated abnormally");
		}
	}

	errno = saved_errno;
}

/* ARGSUSED */
void
sigquit(int signum __unused)
{
	quit = 1;
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-d device]\n", __progname);
	exit(1);
}
