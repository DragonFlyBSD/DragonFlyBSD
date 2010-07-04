/*
 * Temporary debug code to find processes locking internal cryptsetup devices.
 * This code is intended to run only in debug mode.
 *
 * inspired by psmisc/fuser proc scanning code
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "libcryptsetup.h"
#include "internal.h"

#define MAX_PATHNAME 1024
#define MAX_SHORTNAME 64

static int numeric_name(const char *name)
{
	return (name[0] < '0' || name[0] > '9') ? 0 : 1;
}

static int check_pid(const pid_t pid, const char *dev_name, const char *short_dev_name)
{
	char dirpath[MAX_SHORTNAME], fdpath[MAX_SHORTNAME], linkpath[MAX_PATHNAME];
	DIR *dirp;
	struct dirent *direntry;
	size_t len;
	int r = 0;

	snprintf(dirpath, sizeof(dirpath), "/proc/%d/fd", pid);

	if (!(dirp = opendir(dirpath)))
		return r;

	while ((direntry = readdir(dirp))) {
		if (!numeric_name(direntry->d_name))
			continue;

		snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd/%s", pid, direntry->d_name);

		if ((len = readlink(fdpath, linkpath, MAX_PATHNAME-1)) < 0)
			break;
		linkpath[len] = '\0';

		if (!strcmp(dev_name, linkpath)) {
			r = 1;
			break;
		 }

		if (!strcmp(short_dev_name, linkpath)) {
			r = 2;
			break;
		 }
	}
	closedir(dirp);
	return r;
}

static int read_proc_info(const pid_t pid, pid_t *ppid, char *name, int max_size)
{
	char path[MAX_SHORTNAME], info[max_size], c;
	int fd, xpid, r = 0;

	snprintf(path, sizeof(path), "/proc/%u/stat", pid);
	if ((fd = open(path, O_RDONLY)) < 0)
		return 0;

	if (read(fd, info, max_size) > 0 &&
	    sscanf(info, "%d %s %c %d", &xpid, name, &c, ppid) == 4)
		r = 1;

	if (!r) {
		*ppid = 0;
		name[0] = '\0';
	}
	close(fd);
	return r;
}

static void report_proc(const pid_t pid, const char *dev_name)
{
	char name[MAX_PATHNAME], name2[MAX_PATHNAME];
	pid_t ppid, ppid2;

	if (read_proc_info(pid, &ppid, name, MAX_PATHNAME) &&
	    read_proc_info(ppid, &ppid2, name2, MAX_PATHNAME))
		log_dbg("WARNING: Process PID %u %s [PPID %u %s] spying on internal device %s.",
			pid, name, ppid, name2, dev_name);
}

void debug_processes_using_device(const char *dm_name)
{
	char short_dev_name[MAX_SHORTNAME], dev_name[MAX_PATHNAME];
	DIR *proc_dir;
	struct dirent *proc_dentry;
	struct stat st;
	pid_t pid;

	if (crypt_get_debug_level() != CRYPT_LOG_DEBUG)
		return;

	snprintf(dev_name, sizeof(dev_name), "/dev/mapper/%s", dm_name);
	if (stat(dev_name, &st) || !S_ISBLK(st.st_mode))
		return;
	snprintf(short_dev_name, sizeof(short_dev_name), "/dev/dm-%u", minor(st.st_rdev));

	if (!(proc_dir = opendir("/proc")))
		return;

	while ((proc_dentry = readdir(proc_dir))) {
		if (!numeric_name(proc_dentry->d_name))
			continue;

		pid = atoi(proc_dentry->d_name);
		switch(check_pid(pid, dev_name, short_dev_name)) {
		case 1: report_proc(pid, dev_name);
			break;
		case 2: report_proc(pid, short_dev_name);
		default:
			break;
		}
	}
	closedir(proc_dir);
}
