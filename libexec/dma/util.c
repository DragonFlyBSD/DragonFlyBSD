/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon 'corecode' Schubert <corecode@fs.ei.tum.de>.
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

#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

const char *
hostname(void)
{
	static char name[MAXHOSTNAMELEN+1];
	int initialized = 0;
	FILE *fp;
	size_t len;

	if (initialized)
		return (name);

	if (config.mailname != NULL && config.mailname[0] != '\0') {
		snprintf(name, sizeof(name), "%s", config.mailname);
		initialized = 1;
		return (name);
	}
	if (config.mailnamefile != NULL && config.mailnamefile[0] != '\0') {
		fp = fopen(config.mailnamefile, "r");
		if (fp != NULL) {
			if (fgets(name, sizeof(name), fp) != NULL) {
				len = strlen(name);
				while (len > 0 &&
				    (name[len - 1] == '\r' ||
				     name[len - 1] == '\n'))
					name[--len] = '\0';
				if (name[0] != '\0') {
					initialized = 1;
					return (name);
				}
			}
			fclose(fp);
		}
	}
	if (gethostname(name, sizeof(name)) != 0)
		strcpy(name, "(unknown hostname)");
	initialized = 1;
	return name;
}

void
setlogident(const char *fmt, ...)
{
	char *tag = NULL;

	if (fmt != NULL) {
		va_list ap;
		char *sufx;

		va_start(ap, fmt);
		vasprintf(&sufx, fmt, ap);
		if (sufx != NULL) {
			asprintf(&tag, "%s[%s]", logident_base, sufx);
			free(sufx);
		}
		va_end(ap);
	}
	closelog();
	openlog(tag != NULL ? tag : logident_base, 0, LOG_MAIL);
}

void
errlog(int exitcode, const char *fmt, ...)
{
	int oerrno = errno;
	va_list ap;
	char *outs = NULL;

	if (fmt != NULL) {
		va_start(ap, fmt);
		vasprintf(&outs, fmt, ap);
		va_end(ap);
	}

	if (outs != NULL) {
		syslog(LOG_ERR, "%s: %m", outs);
		fprintf(stderr, "%s: %s: %s\n", getprogname(), outs, strerror(oerrno));
	} else {
		syslog(LOG_ERR, "%m");
		fprintf(stderr, "%s: %s\n", getprogname(), strerror(oerrno));
	}

	exit(exitcode);
}

void
errlogx(int exitcode, const char *fmt, ...)
{
	va_list ap;
	char *outs = NULL;

	if (fmt != NULL) {
		va_start(ap, fmt);
		vasprintf(&outs, fmt, ap);
		va_end(ap);
	}

	if (outs != NULL) {
		syslog(LOG_ERR, "%s", outs);
		fprintf(stderr, "%s: %s\n", getprogname(), outs);
	} else {
		syslog(LOG_ERR, "Unknown error");
		fprintf(stderr, "%s: Unknown error\n", getprogname());
	}

	exit(exitcode);
}

static const char *
check_username(const char *name, uid_t ckuid)
{
	struct passwd *pwd;

	if (name == NULL)
		return (NULL);
	pwd = getpwnam(name);
	if (pwd == NULL || pwd->pw_uid != ckuid)
		return (NULL);
	return (name);
}

void
set_username(void)
{
	struct passwd *pwd;
	char *u = NULL;
	uid_t uid;

	uid = getuid();
	username = check_username(getlogin(), uid);
	if (username != NULL)
		return;
	username = check_username(getenv("LOGNAME"), uid);
	if (username != NULL)
		return;
	username = check_username(getenv("USER"), uid);
	if (username != NULL)
		return;
	pwd = getpwuid(uid);
	if (pwd != NULL && pwd->pw_name != NULL && pwd->pw_name[0] != '\0' &&
	    (u = strdup(pwd->pw_name)) != NULL) {
		username = check_username(u, uid);
		if (username != NULL)
			return;
		else
			free(u);
	}
	asprintf(__DECONST(void *, &username), "%ld", (long)uid);
	if (username != NULL)
		return;
	username = "unknown-or-invalid-username";
}

void
deltmp(void)
{
	struct stritem *t;

	SLIST_FOREACH(t, &tmpfs, next) {
		unlink(t->str);
	}
}

int
open_locked(const char *fname, int flags, ...)
{
	int mode = 0;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

#ifndef O_EXLOCK
	int fd, save_errno;

	fd = open(fname, flags, mode);
	if (fd < 0)
		return(fd);
	if (flock(fd, LOCK_EX|((flags & O_NONBLOCK)? LOCK_NB: 0)) < 0) {
		save_errno = errno;
		close(fd);
		errno = save_errno;
		return(-1);
	}
	return(fd);
#else
	return(open(fname, flags|O_EXLOCK, mode));
#endif
}

char *
rfc822date(void)
{
	static char str[50];
	size_t error;
	time_t now;

	now = time(NULL);
	error = strftime(str, sizeof(str), "%a, %d %b %Y %T %z",
		       localtime(&now));
	if (error == 0)
		strcpy(str, "(date fail)");
	return (str);
}

int
strprefixcmp(const char *str, const char *prefix)
{
	return (strncasecmp(str, prefix, strlen(prefix)));
}

