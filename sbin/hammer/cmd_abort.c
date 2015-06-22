/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by John Marino <draco@marino.st>
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
 * Each "hammer cleanup" command creates a pid file at /var/run with the
 * name hammer.cleanup.$pid with the contents of $pid
 *
 * If the cleanup ends without incident, the pid file is removed.  If the
 * cleanup job is interrupted, the pid file is not removed.  This is
 * because SIGINT is disabled in deferrence to HAMMER ioctl.
 *
 * The "hammer abort-cleanup" command is simple.  It scans /var/run for
 * all files starting with "hammer.cleanup.", reads them, and issues a
 * SIGINTR for any valid pid.  Every hammer.cleanup.XXXXXX file will be
 * removed after the command executes.  If multiple cleanup jobs are
 * running simultaneously, all of them will be aborted.
 *
 * It is intended any future "abort" commands are also placed in here.
 */

#include "hammer.h"

void
hammer_cmd_abort_cleanup(char **av __unused, int ac __unused)
{
	DIR *dir;
	pid_t pid;
	char *str;
	int pf_fd;
	struct dirent *den;
	static char pidfile[PIDFILE_BUFSIZE];
	static const char prefix[] = "hammer.cleanup.";
	static const char termmsg[] = "Terminated cleanup process %u\n";
	const size_t pflen = sizeof(prefix) - 1;

	if ((dir = opendir(pidfile_loc)) == NULL) {
	        return;
	}

	while ((den = readdir(dir)) != NULL) {
		if (strncmp(den->d_name, prefix, pflen) == 0) {
			snprintf (pidfile, PIDFILE_BUFSIZE, "%s/%s",
				pidfile_loc, den->d_name);
			pid = strtol((char *)(den->d_name + pflen), &str, 10);
			pf_fd = open(pidfile, O_RDONLY | O_CLOEXEC);
			if (pf_fd == -1) {
				continue;
			}

			if (flock(pf_fd, LOCK_EX | LOCK_NB) < 0) {
				if (errno == EWOULDBLOCK) {
					/* error expected during cleanup */
					if (kill (pid, SIGTERM) == 0) {
						printf (termmsg, pid);
					}
				}
			}
			else {
				/* lock succeeded so pidfile is stale */
				flock (pf_fd, LOCK_UN);
			}
			close (pf_fd);
			unlink (pidfile);
		}
	}
	closedir(dir);
}
