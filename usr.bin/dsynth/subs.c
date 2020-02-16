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

extern char **environ;

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

char *
strdup_or_null(char *str)
{
	if (str && str[0])
		return(strdup(str));
	return NULL;
}

static const char *DLogNames[] = {
	"00_last_results.log",
	"01_success_list.log",
	"02_failure_list.log",
	"03_ignored_list.log",
	"04_skipped_list.log",
	"05_abnormal_command_output.log",
	"06_obsolete_packages.log",
	"07_debug.log",
};

static int DLogFd[DLOG_COUNT];
static pthread_mutex_t DLogFdMutex;

#define arysize(ary)	(sizeof((ary)) / sizeof((ary)[0]))

static int
dlogfd(int which, int modes)
{
	char *path;
	int fd;

	which &= DLOG_MASK;
	if ((fd = DLogFd[which]) > 0)
		return fd;
	pthread_mutex_lock(&DLogFdMutex);
	if ((fd = DLogFd[which]) <= 0) {
		asprintf(&path, "%s/%s", LogsPath, DLogNames[which]);
		fd = open(path, modes, 0666);
		DLogFd[which] = fd;
		free(path);
	}
	pthread_mutex_unlock(&DLogFdMutex);

	return fd;
}


void
dlogreset(void)
{
	int i;

	ddassert(DLOG_COUNT == arysize(DLogNames));
	for (i = 0; i < DLOG_COUNT; ++i) {
		if (DLogFd[i] > 0) {
			close(DLogFd[i]);
			DLogFd[i] = -1;
		}
		(void)dlogfd(i, O_RDWR|O_CREAT|O_TRUNC|O_APPEND);
	}
}

void
_dlog(int which, const char *ctl, ...)
{
	va_list va;
	char *buf;
	char *ptr;
	size_t len;
	int fd;
	int filter;

	filter = which;
	which &= DLOG_MASK;

	ddassert((uint)which < DLOG_COUNT);
	va_start(va, ctl);
	vasprintf(&buf, ctl, va);
	va_end(va);
	len = strlen(buf);

	/*
	 * The special sequence ## right-justfies the text after the ##.
	 *
	 * NOTE: Alignment test includes \n so use 80 instead of 79 to
	 *	 leave one char unused on a 80-column terminal.
	 */
	if ((ptr = strstr(buf, "##")) != NULL) {
		size_t l2;
		size_t l1;
		char *b2;
		int spc;

		l1 = (int)(ptr - buf);
		l2 = len - l1 - 2;
		if (l1 <= 80 - l2) {
			spc = 80 - l1 - l2;
			buf[l1] = 0;
			asprintf(&b2, "%s%*.*s%s",
				 buf, spc, spc, "", ptr + 2);
		} else {
			buf[l1] = 0;
			asprintf(&b2, "%s%s", buf, ptr + 2);
		}
		len = strlen(b2);
		free(buf);
		buf = b2;
	}

	/*
	 * All logs also go to log 00.
	 */
	if (which != DLOG_ALL) {
		fd = dlogfd(DLOG_ALL, O_RDWR|O_CREAT|O_APPEND);
		if (fd > 0)
			write(fd, buf, len);
	}

	/*
	 * Nominal log target
	 */
	fd = dlogfd(which, O_RDWR|O_CREAT|O_APPEND);
	write(fd, buf, len);

	/*
	 * If ncurses is not being used, all log output also goes
	 * to stdout, unless filtered.
	 */
	if ((UseNCurses == 0 || (filter & DLOG_STDOUT)) &&
	    (filter & DLOG_FILTER) == 0) {
		if (ColorOpt) {
			if (filter & DLOG_GRN)
				write(1, "\x1b[0;32m", 7);
			if (filter & DLOG_RED)
				write(1, "\x1b[0;31m", 7);
		}
		write(1, buf, len);
		if (ColorOpt && (filter & (DLOG_GRN|DLOG_RED))) {
			write(1, "\x1b[0;39m", 7);
		}
	}
	free(buf);
}

int
dlog00_fd(void)
{
	return(dlogfd(DLOG_ALL, O_RDWR|O_CREAT|O_APPEND));
}

/*
 * Bulk and Build environment control.  These routines are only called
 * unthreaded or when dsynth threads are idle.
 */
void
addbuildenv(const char *label, const char *data, int type)
{
	buildenv_t *env;

	env = calloc(1, sizeof(*env));
	env->a1 = strdup(label);
	env->a2 = strdup(data);
	env->label = env->a1;
	env->data = env->a2;
	env->type = type;
	*BuildEnvTail = env;
	BuildEnvTail = &env->next;
}

void
delbuildenv(const char *label)
{
	buildenv_t **envp;
	buildenv_t *env;

	envp = &BuildEnv;
	while ((env = *envp) != NULL) {
		if (strcmp(env->label, label) == 0) {
			*envp = env->next;
			if (env->a1)
				free(env->a1);
			if (env->a2)
				free(env->a2);
			free(env);
		} else {
			envp = &env->next;
		}
	}
	BuildEnvTail = envp;
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

int
askyn(const char *ctl, ...)
{
	va_list va;
	char buf[256];
	int res = 0;

	if (YesOpt)
		return 1;

	for (;;) {
		va_start(va, ctl);
		vprintf(ctl, va);
		va_end(va);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		if (buf[0] == 'y' || buf[0] == 'Y') {
			res = 1;
			break;
		}
		if (buf[0] == 'n' || buf[0] == 'N') {
			res = 0;
			break;
		}
		printf("Please type y/n\n");
	}
	return res;
}

/*
 * Get swap% used 0.0-1.0.
 *
 * NOTE: This routine is intended to return quickly.
 *
 * NOTE: swap_cache (caching for hard drives) is persistent and should
 *	 not be counted for our purposes.
 */
double
getswappct(int *noswapp)
{
	long swap_size = 0;
	long swap_anon = 0;
	long swap_cache __unused = 0;
	size_t len;
	double dswap;

	len = sizeof(swap_size);
	sysctlbyname("vm.swap_size", &swap_size, &len, NULL, 0);
	len = sizeof(swap_size);
	sysctlbyname("vm.swap_anon_use", &swap_anon, &len, NULL, 0);
	len = sizeof(swap_size);
	sysctlbyname("vm.swap_cache_use", &swap_cache, &len, NULL, 0);
	if (swap_size) {
		dswap = (double)(swap_anon /*+swap_cache*/) / (double)swap_size;
		*noswapp = 0;
	} else {
		dswap = 0.0;
		*noswapp = 1;
	}
	return dswap;
}

/*
 * dexec_open()/fgets/dexec_close()
 *
 * Similar to popen() but directly exec()s the argument list (cav[0] must
 * be an absolute path).
 *
 * If xenv is non-NULL its an array of local buildenv_t's to be used.
 * The array is terminated with a NULL xenv->label.
 *
 * If with_env is non-zero the configured environment is included.
 *
 * If with_mvars is non-zero the make environment is passed as VAR=DATA
 * elements on the command line.
 */
FILE *
dexec_open(const char **cav, int cac, pid_t *pidp, buildenv_t *xenv,
	   int with_env, int with_mvars)
{
	buildenv_t *benv;
	const char **cenv;
	char *allocary[MAXCAC*2];
	int env_basei;
	int envi;
	int alloci;
	int nullfd;
	int fds[2];
	pid_t pid;
	FILE *fp;

	env_basei = 0;
	while (environ[env_basei])
		++env_basei;
	cenv = calloc(env_basei + MAXCAC, sizeof(char *));
	env_basei = 0;
	for (envi = 0; envi < env_basei; ++envi)
		cenv[envi] = environ[envi];

	alloci = 0;
	for (benv = BuildEnv; benv; benv = benv->next) {
		if (with_mvars &&
		    (benv->type & BENV_CMDMASK) == BENV_MAKECONF) {
			asprintf(&allocary[alloci], "%s=%s",
				 benv->label, benv->data);
			cav[cac++] = allocary[alloci];
			++alloci;
		}
		if (with_env &&
		    (benv->type & BENV_PKGLIST) &&
		    (benv->type & BENV_CMDMASK) == BENV_ENVIRONMENT) {
			asprintf(&allocary[alloci], "%s=%s",
				 benv->label, benv->data);
			cenv[envi++] = allocary[alloci];
			++alloci;
		}
		ddassert(cac < MAXCAC && envi - env_basei < MAXCAC);
	}

	/*
	 * Extra environment specific to this particular dexec
	 */
	while (xenv && xenv->label) {
		asprintf(&allocary[alloci], "%s=%s",
			 xenv->label, xenv->data);
		cenv[envi++] = allocary[alloci];
		++alloci;
		++xenv;
	}

	cav[cac] = NULL;
	cenv[envi] = NULL;

	if (pipe(fds) < 0)
		dfatal_errno("pipe");
	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0)
		dfatal_errno("open(\"/dev/null\")");

	/*
	 * We have to be very careful using vfork(), do only the bare
	 * minimum necessary in the child to set it up and exec it.
	 */
	pid = vfork();
	if (pid == 0) {
#if 0
		int i;
		printf("%s", cav[0]);
		for (i = 0; cav[i]; ++i)
			printf(" %s", cav[i]);
		printf("\n");
		printf("ENV: ");
		for (i = 0; cenv[i]; ++i)
			printf(" %s", cenv[i]);
#endif

		if (fds[1] != 1) {
			dup2(fds[1], 1);
			close(fds[1]);
		}
		close(fds[0]);		/* safety */
		dup2(nullfd, 0);	/* no questions! */
		closefrom(3);		/* be nice */

		execve(cav[0], (void *)cav, (void *)cenv);
		write(2, "EXEC FAILURE\n", 13);
		_exit(1);
	}
	close(nullfd);
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		dfatal_errno("vfork failed");
	}
	fp = fdopen(fds[0], "r");
	*pidp = pid;

	while (--alloci >= 0)
		free(allocary[alloci]);
	free(cenv);

	return fp;
}

int
dexec_close(FILE *fp, pid_t pid)
{
	pid_t rpid;
	int status;

	fclose(fp);
	while ((rpid = waitpid(pid, &status, 0)) != pid) {
		if (rpid < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
	}
	return (WEXITSTATUS(status));
}

const char *
getphasestr(worker_phase_t phaseid)
{
	const char *phase;

	switch(phaseid) {
	case PHASE_PENDING:
		phase = "pending";
		break;
	case PHASE_INSTALL_PKGS:
		phase = "install-pkgs";
		break;
	case PHASE_CHECK_SANITY:
		phase = "check-sanity";
		break;
	case PHASE_PKG_DEPENDS:
		phase = "pkg-depends";
		break;
	case PHASE_FETCH_DEPENDS:
		phase = "fetch-depends";
		break;
	case PHASE_FETCH:
		phase = "fetch";
		break;
	case PHASE_CHECKSUM:
		phase = "checksum";
		break;
	case PHASE_EXTRACT_DEPENDS:
		phase = "extract-depends";
		break;
	case PHASE_EXTRACT:
		phase = "extract";
		break;
	case PHASE_PATCH_DEPENDS:
		phase = "patch-depends";
		break;
	case PHASE_PATCH:
		phase = "patch";
		break;
	case PHASE_BUILD_DEPENDS:
		phase = "build-depends";
		break;
	case PHASE_LIB_DEPENDS:
		phase = "lib-depends";
		break;
	case PHASE_CONFIGURE:
		phase = "configure";
		break;
	case PHASE_BUILD:
		phase = "build";
		break;
	case PHASE_RUN_DEPENDS:
		phase = "run-depends";
		break;
	case PHASE_STAGE:
		phase = "stage";
		break;
	case PHASE_TEST:
		phase = "test";
		break;
	case PHASE_CHECK_PLIST:
		phase = "check-plist";
		break;
	case PHASE_PACKAGE:
		phase = "package";
		break;
	case PHASE_INSTALL_MTREE:
		phase = "install-mtree";
		break;
	case PHASE_INSTALL:
		phase = "install";
		break;
	case PHASE_DEINSTALL:
		phase = "deinstall";
		break;
	default:
		phase = "Run-Unknown";
		break;
	}
	return phase;
}

int
readlogline(monitorlog_t *log, char **bufp)
{
	int r;
	int n;

	/*
	 * Reset buffer as an optimization to avoid unnecessary
	 * shifts.
	 */
	*bufp = NULL;
	if (log->buf_beg == log->buf_end) {
		log->buf_beg = 0;
		log->buf_end = 0;
		log->buf_scan = 0;
	}

	/*
	 * Look for newline, handle discard mode
	 */
again:
	for (n = log->buf_scan; n < log->buf_end; ++n) {
		if (log->buf[n] == '\n') {
			*bufp = log->buf + log->buf_beg;
			r = n - log->buf_beg;
			log->buf_beg = n + 1;
			log->buf_scan = n + 1;

			if (log->buf_discard_mode == 0)
				return r;
			log->buf_discard_mode = 0;
			goto again;
		}
	}

	/*
	 * Handle overflow
	 */
	if (n == sizeof(log->buf)) {
		if (log->buf_beg) {
			/*
			 * Shift the buffer to make room and read more data.
			 */
			bcopy(log->buf + log->buf_beg,
			      log->buf,
			      n - log->buf_beg);
			log->buf_end -= log->buf_beg;
			log->buf_scan -= log->buf_beg;
			n -= log->buf_beg;
			log->buf_beg = 0;
		} else if (log->buf_discard_mode) {
			/*
			 * Overflow.  If in discard mode just throw it all
			 * away.  Stay in discard mode.
			 */
			log->buf_beg = 0;
			log->buf_end = 0;
			log->buf_scan = 0;
		} else {
			/*
			 * Overflow.  If not in discard mode return a truncated
			 * line and enter discard mode.
			 *
			 * The caller will temporarily set ptr[r] = 0 so make
			 * sure that does not overflow our buffer as we are not
			 * at a newline.
			 *
			 * (log->buf_beg is 0);
			 */
			*bufp = log->buf + log->buf_beg;
			r = n - 1;
			log->buf_beg = n;
			log->buf_scan = n;
			log->buf_discard_mode = 1;

			return r;
		}
	}

	/*
	 * Read more data.  If there is no data pending then return -1,
	 * otherwise loop up to see if more complete line(s) are available.
	 */
	r = pread(log->fd,
		  log->buf + log->buf_end,
		  sizeof(log->buf) - log->buf_end,
		  log->offset);
	if (r <= 0)
		return -1;
	log->offset += r;
	log->buf_end += r;
	goto again;
}
