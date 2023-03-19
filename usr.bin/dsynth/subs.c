/*
 * Copyright (c) 2019-2020 The DragonFly Project.  All rights reserved.
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

struct logerrinfo {
	struct logerrinfo *next;
	char	*logid;
	pid_t	pid;
	long	seq;
	int	exited;
};

buildenv_t *BuildEnv;
static buildenv_t **BuildEnvTail = &BuildEnv;

extern char **environ;

static void *dexec_logerr_thread(void *info);

#define EINFO_HSIZE	1024
#define EINFO_HMASK	(EINFO_HSIZE - 1)

static struct logerrinfo *EInfo[EINFO_HSIZE];
static pthread_t ETid;
static int EFds[2];
static pthread_cond_t ECond;

static __inline
struct logerrinfo **
einfohash(pid_t pid)
{
	return(&EInfo[pid & EINFO_HMASK]);
}

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

const char *
getbuildenv(const char *label)
{
	buildenv_t **envp;
	buildenv_t *env;

	envp = &BuildEnv;
	while ((env = *envp) != NULL) {
		if (strcmp(env->label, label) == 0)
			return env->data;
		envp = &env->next;
	}
	return NULL;
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
dexec_open(const char *logid, const char **cav, int cac,
	   pid_t *pidp, buildenv_t *xenv, int with_env, int with_mvars)
{
	buildenv_t *benv;
	const char **cenv;
	char *allocary[MAXCAC*2];
	int env_basei;
	int envi;
	int alloci;
	int nullfd;
	int fds[2];	/* stdout */
	pid_t pid;
	FILE *fp;
	struct logerrinfo *einfo;
	static int warned;

	/*
	 * Error logging thread setup
	 */
	if (ETid == 0) {
		pthread_mutex_lock(&DLogFdMutex);
		if (ETid == 0) {
			if (socketpair(AF_UNIX, SOCK_DGRAM, 0, EFds) < 0)
				dfatal_errno("socketpair");
#ifdef SO_PASSCRED
			int optval = 1;
			if (setsockopt(EFds[0], SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) < 0) {
				if (warned == 0) {
					warned = 1;
					fprintf(stderr, "SO_PASSCRED not supported\n");
				}
			}
#endif

			pthread_cond_init(&ECond, NULL);
			pthread_create(&ETid, NULL, dexec_logerr_thread, NULL);
		}
		pthread_mutex_unlock(&DLogFdMutex);
	}

	env_basei = 0;
#if 0
	/*
	 * For now we are ignoring the passed-in environment, so don't
	 * copy the existing environment into the exec environment.
	 */
	while (environ[env_basei])
		++env_basei;
#endif
	cenv = calloc(env_basei + MAXCAC, sizeof(char *));
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

	if (pipe2(fds, O_CLOEXEC) < 0)
		dfatal_errno("pipe");
	nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
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
		if (EFds[1] != 2) {
			dup2(EFds[1], 2);
			close(EFds[1]);
		}
		close(fds[0]);		/* safety */
		close(EFds[0]);		/* safety */
		dup2(nullfd, 0);	/* no questions! */
		closefrom(3);		/* be nice */

		/*
		 * Self-nice to be nice (ignore any error)
		 */
		if (NiceOpt)
			setpriority(PRIO_PROCESS, 0, NiceOpt);

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

	/*
	 * einfo setup
	 */
	einfo = calloc(1, sizeof(*einfo));
	if (logid)
		einfo->logid = strdup(logid);

	pthread_mutex_lock(&DLogFdMutex);
	einfo->pid = pid;
	einfo->next = *einfohash(pid);
	*einfohash(pid) = einfo;
	pthread_cond_signal(&ECond);
	pthread_mutex_unlock(&DLogFdMutex);

	while (--alloci >= 0)
		free(allocary[alloci]);
	free(cenv);

	return fp;
}

int
dexec_close(FILE *fp, pid_t pid)
{
	struct logerrinfo **einfop;
	struct logerrinfo *einfo;
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

	pthread_mutex_lock(&DLogFdMutex);
	einfop = einfohash(pid);
	while ((einfo = *einfop) != NULL) {
		if (einfo->pid == pid) {
			einfo->exited = 1;
			break;
		}
		einfop = &einfo->next;
	}
	pthread_mutex_unlock(&DLogFdMutex);

	return (WEXITSTATUS(status));
}

static void *
dexec_logerr_thread(void *dummy __unused)
{
	char buf[4096];
	union {
		char cbuf[CMSG_SPACE(sizeof(struct ucred))];
		struct cmsghdr cbuf_align;
	} cmsg_buf;
	struct cmsghdr *cmsg;
	struct logerrinfo **einfop;
	struct logerrinfo *einfo;
	struct msghdr msg;
	struct iovec iov;
	ssize_t len;
	int mflags = MSG_DONTWAIT;
	int fd;

	pthread_detach(pthread_self());

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;

	msg.msg_control = cmsg_buf.cbuf;
	msg.msg_controllen = sizeof(cmsg_buf.cbuf);

	fd = EFds[0];
	for (;;) {
		int i;

		msg.msg_iovlen = 1;
		iov.iov_base = buf;
		iov.iov_len = sizeof(buf) - 1;

		/*
		 * Wait for message, if none are pending then clean-up
		 * exited einfos (this ensures that we have flushed all
		 * error output before freeing the structure).
		 *
		 * Don't obtain the mutex until we really need it.  The
		 * parent thread can only 'add' einfo entries to the hash
		 * table so we are basically scan-safe.
		 */
		len = recvmsg(fd, &msg, mflags);
		if (len < 0) {
			if (errno != EAGAIN) {	/* something messed up */
				fprintf(stderr, "ERRNO %d\n", errno);
				break;
			}

			for (i = 0; i < EINFO_HSIZE; ++i) {
				einfo = EInfo[i];
				while (einfo) {
					if (einfo->exited)
						break;
					einfo = einfo->next;
				}
				if (einfo == NULL)
					continue;
				pthread_mutex_lock(&DLogFdMutex);
				einfop = &EInfo[i];
				while ((einfo = *einfop) != NULL) {
					if (einfo->exited) {
						*einfop = einfo->next;
						if (einfo->logid)
							free(einfo->logid);
						free(einfo);
					} else {
						einfop = &einfo->next;
					}
				}
				pthread_mutex_unlock(&DLogFdMutex);
			}
			mflags = 0;
			continue;
		}

		/*
		 * Process SCM_CREDS, if present.  Throw away SCM_RIGHTS
		 * if some sub-process stupidly sent it.
		 */
		einfo = NULL;
		mflags = MSG_DONTWAIT;

		if (len && buf[len-1] == '\n')
			--len;
		buf[len] = 0;

		for (cmsg = CMSG_FIRSTHDR(&msg);
		     cmsg;
		     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			struct cmsgcred *cred;
			int *fds;
			int n;

			if (cmsg->cmsg_level != SOL_SOCKET)
				continue;

			switch(cmsg->cmsg_type) {
			case SCM_CREDS:

				cred = (void *)CMSG_DATA(cmsg);

				einfo = *einfohash(cred->cmcred_pid);
				while (einfo && einfo->pid != cred->cmcred_pid)
					einfo = einfo->next;
				break;
			case SCM_RIGHTS:
				fds = (void *)CMSG_DATA(cmsg);
				n = (cmsg->cmsg_len - sizeof(cmsg)) /
				    sizeof(int);
				for (i = 0; i < n; ++i)
					close(fds[i]);
				break;
			}
		}

		if (einfo && einfo->logid) {
			dlog(DLOG_ALL | DLOG_STDOUT,
			     "%s: %s\n",
			     einfo->logid, buf);
		} else {
			dlog(DLOG_ALL | DLOG_STDOUT, "%s", buf);
		}
	}
	return NULL;
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
	case PHASE_INSTALL:
		phase = "install";
		break;
	case PHASE_DEINSTALL:
		phase = "deinstall";
		break;
	case PHASE_DUMP_ENV:
		phase = "dump-env";
		break;
	case PHASE_DUMP_VAR:
		phase = "dump-var";
		break;
	case PHASE_SHOW_CONFIG:
		phase = "show-config";
		break;
	case PHASE_DUMP_MAKECONF:
		phase = "make-conf";
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

uint32_t
crcDirTree(const char *path)
{
	FTS *fts;
	FTSENT *fen;
	struct stat *st;
	char *pav[2];
	uint32_t crc;
	uint32_t val;

	crc = 0;
	pav[0] = strdup(path);
	pav[1] = NULL;

	fts = fts_open(pav, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (fts == NULL)
		goto failed;
	while ((fen = fts_read(fts)) != NULL) {
		if (fen->fts_info != FTS_F && fen->fts_info != FTS_SL)
			continue;
		/*
		 * Ignore hidden dot files or ones ending with .core from the
		 * calculated CRC sum to prevent unnecessary rebuilds.
		 */
		if (fen->fts_name[0] == '.')
			continue;
		if (fen->fts_namelen >= 5 &&
		    !strcmp(fen->fts_name + fen->fts_namelen - 5, ".core")) {
			continue;
		}

		st = fen->fts_statp;

		val = iscsi_crc32(&st->st_mtime, sizeof(st->st_mtime));
		val = iscsi_crc32_ext(&st->st_size, sizeof(st->st_size), val);
		val = iscsi_crc32_ext(fen->fts_path, fen->fts_pathlen, val);
		crc ^= val;
	}
	fts_close(fts);
failed:
	free(pav[0]);

	return crc;
}
