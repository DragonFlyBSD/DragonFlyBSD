/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code uses concepts and configuration based on 'synth', by
 * John R. Marino <draco@marino.st>, which was written in ada.
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

#include "dsynth.h"

buildenv_t *BuildEnv;
static buildenv_t **BuildEnvTail = &BuildEnv;

__dead2 void
_dfatal(const char *file __unused, int line __unused, const char *func,
	int do_errno, const char *ctl, ...)
{
	va_list va;

	fprintf(stderr, "%s: ", func);
	va_start(va, ctl);
	vfprintf(stderr, ctl, va);
	va_end(va);
	if (do_errno & 1)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	fflush(stderr);

	if (do_errno & 2)
		kill(getpid(), SIGQUIT);
	exit(1);
}

void
_ddprintf(int tab, const char *ctl, ...)
{
	va_list va;

	if (tab)
		printf("%*.*s", tab, tab, "");
	va_start(va, ctl);
	vfprintf(stdout, ctl, va);
	va_end(va);
}

static const char *DLogNames[] = {
	"00_last_results.log",
	"01_success_list.log",
	"02_failure_list.log",
	"03_ignored_list.log",
	"04_skipped_list.log",
	"05_abnormal_command_output.log",
	"06_obsolete_packages.log"
};

#define arysize(ary)	(sizeof((ary)) / sizeof((ary)[0]))

void
dlogreset(void)
{
	char *path;
	int i;
	int fd;

	ddassert(DLOG_COUNT == arysize(DLogNames));
	for (i = 0; i < DLOG_COUNT; ++i) {
		asprintf(&path, "%s/%s", LogsPath, DLogNames[i]);
		remove(path);
		fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);
		if (fd >= 0)
			close(fd);
		free(path);
	}
}

void
_dlog(int which, const char *ctl, ...)
{
	va_list va;
	char path[256];
	char *buf;
	size_t len;
	int fd;

	ddassert((uint)which < DLOG_COUNT);
	va_start(va, ctl);
	vasprintf(&buf, ctl, va);
	va_end(va);
	len = strlen(buf);

	if (which != DLOG_ALL) {
		snprintf(path, sizeof(path),
			 "%s/%s", LogsPath, DLogNames[0]);
		fd = open(path, O_RDWR|O_CREAT|O_APPEND, 0644);
		if (fd >= 0) {
			write(fd, buf, len);
			close(fd);
		}
	}
	snprintf(path, sizeof(path), "%s/%s", LogsPath, DLogNames[which]);
	fd = open(path, O_RDWR|O_CREAT|O_APPEND, 0644);
	if (fd >= 0) {
		write(fd, buf, len);
		close(fd);
	}
}

void
addbuildenv(const char *label, const char *data)
{
	buildenv_t *env;

	env = calloc(1, sizeof(*env));
	env->label = strdup(label);
	env->data = strdup(data);
	*BuildEnvTail = env;
	BuildEnvTail = &env->next;
}

void
freestrp(char **strp)
{
	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
}

void
dupstrp(char **strp)
{
	if (*strp)
		*strp = strdup(*strp);
}

int
ipcreadmsg(int fd, wmsg_t *msg)
{
	size_t res;
	ssize_t r;
	char *ptr;

	res = sizeof(*msg);
	ptr = (char *)(void *)msg;
	while (res) {
		r = read(fd, ptr, res);
		if (r <= 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		res -= (size_t)r;
		ptr += r;
	}
	return 0;
}

int
ipcwritemsg(int fd, wmsg_t *msg)
{
	size_t res;
	ssize_t r;
	char *ptr;

	res = sizeof(*msg);
	ptr = (char *)(void *)msg;
	while (res) {
		r = write(fd, ptr, res);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		res -= (size_t)r;
		ptr += r;
	}
	return 0;
}
