/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Copyright (c) 1980, 1986, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)reboot.c	8.1 (Berkeley) 6/5/93
 * $FreeBSD: src/sbin/reboot/reboot.c,v 1.9.2.4 2002/04/28 22:50:00 wes Exp $
 */

#include <sys/kenv.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <pwd.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>

#define PATH_NEXTBOOT "/boot/nextboot.conf"

/*
 * The loader reads at most this many bytes of nextboot.conf.
 */
#define NEXTBOOT_MAX_SIZE	1024

#define GETOPT_REBOOT	"Dde:fk:lno:pq"
#define GETOPT_NEXTBOOT	"aDe:fk:o:"

static void usage(void) __dead2;
static u_int get_pageins(void);
static void write_nextboot(const char *, const char *, int, int);
static char *split_kv(char *);
static void add_env(char **, const char *, const char *);

static int dohalt;
static int donextboot;

#define E(...) do {				\
		if (force) {			\
			warn(__VA_ARGS__);	\
			return;			\
		}				\
		err(1, __VA_ARGS__);		\
	} while (0)

/*
 * Report a failed write of the nextboot configuration: remove the
 * temporary file, close the stream, then report.  With -f the caller
 * only warns and returns.
 */
#define NEXTBOOT_E(...) do {					\
		int e_;						\
								\
		e_ = errno;					\
		if (unlink(tmp) != 0)				\
			warn("unlink %s", tmp);			\
		if (fp != NULL)					\
			fclose(fp);				\
		errno = e_;					\
		E(__VA_ARGS__);					\
	} while (0)

/*
 * Names and values are written into a file the loader parses, so only
 * accept what fits a loader.conf line.
 */
static int
valid_name(const char *name)
{
	if (*name == '\0')
		return (0);
	for (; *name != '\0'; ++name) {
		if ((*name >= 'A' && *name <= 'Z') ||
		    (*name >= 'a' && *name <= 'z') ||
		    (*name >= '0' && *name <= '9') ||
		    *name == '_' || *name == '.')
			continue;
		return (0);
	}
	return (1);
}

static void
add_env(char **env, const char *key, const char *value)
{
	char *oldenv;

	if (!valid_name(key))
		errx(1, "Invalid variable name '%s'", key);
	if (*value == '\0')
		errx(1, "Invalid empty value for '%s'", key);
	if (strlen(key) >= KENV_MNAMELEN || strlen(value) >= KENV_MVALLEN)
		errx(1, "'%s' or its value exceeds the kernel environment "
		    "limit of %d bytes", key, KENV_MVALLEN);
	if (strchr(value, '"') != NULL || strchr(value, '\n') != NULL)
		errx(1, "Invalid value '%s'", value);
	oldenv = *env;
	if (asprintf(env, "%s%s=\"%s\"\n", oldenv != NULL ? oldenv : "",
	    key, value) == -1)
		errx(1, "No memory to build env array");
	free(oldenv);
}

static char *
split_kv(char *raw)
{
	char *eq;
	int len;

	eq = strchr(raw, '=');
	if (eq == NULL)
		errx(1, "No = in environment string %s", raw);
	*eq++ = '\0';
	len = strlen(eq);
	if (len == 0)
		errx(1, "Invalid null value %s=", raw);
	if (eq[0] == '"') {
		if (len < 2 || eq[len - 1] != '"')
			errx(1, "Invalid string '%s'", eq);
		eq[len - 1] = '\0';
		return (eq + 1);
	}
	return (eq);
}

/*
 * Write the nextboot configuration.  The enabling line goes first, so
 * the loader always sees it, and the file keeps the same size when the
 * loader rewrites that line.  Nothing is installed unless the whole
 * file was written, is not larger than what the loader reads, and has
 * reached the disk.
 */
static void
write_nextboot(const char *fn, const char *env, int append, int force)
{
	char buf[1024];
	char tmp[PATH_MAX];
	FILE *fp, *ofp;
	struct stat st;
	struct statfs sfs;
	size_t blen;
	int endnl, fd, tmpfd;

	if (statfs("/boot", &sfs) != 0)
		err(1, "statfs /boot");
	if (strcmp(sfs.f_fstypename, "ufs") != 0) {
		if (!force)
			errx(1, "/boot is %s, not ufs", sfs.f_fstypename);
		warnx("/boot is %s, not ufs", sfs.f_fstypename);
	}

	if (strlcpy(tmp, fn, sizeof(tmp)) >= sizeof(tmp))
		E("Path too long %s", fn);
	if (strlcat(tmp, ".XXXXXX", sizeof(tmp)) >= sizeof(tmp))
		E("Path too long %s", fn);

	tmpfd = mkstemp(tmp);
	if (tmpfd == -1)
		E("mkstemp %s", tmp);

	fp = fdopen(tmpfd, "w");
	if (fp == NULL) {
		close(tmpfd);
		unlink(tmp);
		E("fdopen %s", tmp);
	}

	if (fprintf(fp, "nextboot_enable=\"YES\"\n") < 0)
		NEXTBOOT_E("Can't write %s", tmp);

	/*
	 * Appending keeps the old settings, but drops the old enabling
	 * line: a consumed one would otherwise follow the new one.
	 */
	if (append) {
		fd = open(fn, O_RDONLY);
		if (fd < 0) {
			if (errno != ENOENT)
				NEXTBOOT_E("open %s", fn);
		} else if ((ofp = fdopen(fd, "r")) == NULL) {
			warn("fdopen %s", fn);
			close(fd);
		} else {
			endnl = 1;
			while (fgets(buf, sizeof(buf), ofp) != NULL) {
				if (strncmp(buf, "nextboot_enable", 15) == 0)
					continue;
				if (fputs(buf, fp) < 0)
					break;
				blen = strlen(buf);
				if (blen != 0)
					endnl = buf[blen - 1] == '\n';
			}
			fclose(ofp);
			if (!endnl && fputc('\n', fp) == EOF)
				NEXTBOOT_E("Can't write %s", tmp);
		}
	}

	if (env != NULL && fprintf(fp, "%s", env) < 0)
		NEXTBOOT_E("Can't write %s", tmp);
	if (fflush(fp) != 0)
		NEXTBOOT_E("Can't write %s", tmp);
	if (fstat(tmpfd, &st) != 0)
		NEXTBOOT_E("fstat %s", tmp);
	if (st.st_size > NEXTBOOT_MAX_SIZE) {
		if (unlink(tmp) != 0)
			warn("unlink %s", tmp);
		fclose(fp);
		fp = NULL;
		errx(1, "%s is larger than %d bytes", fn, NEXTBOOT_MAX_SIZE);
	}
	if (fsync(fileno(fp)) != 0)
		NEXTBOOT_E("Can't fsync %s", tmp);
	if (fclose(fp) != 0) {
		fp = NULL;	/* the error path must not close it again */
		NEXTBOOT_E("Can't close %s", tmp);
	}
	fp = NULL;
	if (rename(tmp, fn) != 0)
		NEXTBOOT_E("Can't rename %s to %s", tmp, fn);
}

int
main(int argc, char *argv[])
{
	struct passwd *pw;
	int ch, howto = 0, i, lflag, nflag, qflag, sverrno;
	int aflag, Dflag, fflag;
	u_int pageins;
	char *env = NULL, *v;
	const char *getopts = GETOPT_REBOOT, *kernel = NULL, *progname;
	const char *user;

	progname = getprogname();
	if (strncmp(progname, "fast", 4) == 0)
		progname += 4;
	if (strcmp(progname, "halt") == 0) {
		dohalt = 1;
		howto = RB_HALT;
	} else if (strcmp(progname, "nextboot") == 0) {
		donextboot = 1;
		getopts = GETOPT_NEXTBOOT;
	}
	aflag = Dflag = fflag = 0;
	lflag = nflag = qflag = 0;
	while ((ch = getopt(argc, argv, getopts)) != -1)
		switch(ch) {
		case 'a':
			aflag = 1;
			break;
		case 'D':
			Dflag = 1;
			break;
		case 'd':
			howto |= RB_DUMP;
			break;
		case 'e':
			v = split_kv(optarg);
			add_env(&env, optarg, v);
			break;
		case 'f':
			fflag = 1;
			break;
		case 'k':
			kernel = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'n':
			nflag = 1;
			howto |= RB_NOSYNC;
			break;
		case 'o':
			add_env(&env, "kernel_options", optarg);
			break;
		case 'p':
			howto |= (RB_POWEROFF | RB_HALT);
			break;
		case 'q':
			qflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();

	if (Dflag && ((howto & ~RB_HALT) != 0 || kernel != NULL ||
	    env != NULL || aflag))
		errx(1, "cannot delete existing nextboot config and do "
		    "anything else");
	if ((howto & (RB_DUMP | RB_HALT)) == (RB_DUMP | RB_HALT))
		errx(1, "cannot dump (-d) when halting; must reboot instead");

	if (Dflag) {
		struct stat sb;

		if (stat(PATH_NEXTBOOT, &sb) != 0 && errno == ENOENT)
			exit(0);
		if (unlink(PATH_NEXTBOOT) != 0) {
			if (errno == ENOENT)
				exit(0);
			warn("unlink " PATH_NEXTBOOT);
			exit(1);
		}
		exit(0);
	}

	if (!donextboot && geteuid()) {
		errno = EPERM;
		err(1, NULL);
	}

	if (kernel != NULL) {
		if (!fflag) {
			char *k;
			struct stat sb;

			if (asprintf(&k, "/boot/%s/kernel", kernel) == -1)
				errx(1, "No memory to check %s", kernel);
			if (stat(k, &sb) != 0)
				err(1, "stat %s", k);
			if (!S_ISREG(sb.st_mode))
				errx(1, "%s is not a file", k);
			free(k);
		}
		add_env(&env, "kernel", kernel);
	}

	if (env != NULL)
		write_nextboot(PATH_NEXTBOOT, env, aflag, fflag);
	if (donextboot)
		exit(0);
	if (qflag) {
		reboot(howto);
		err(1, NULL);
	}

	/* Log the reboot. */
	if (!lflag)  {
		if ((user = getlogin()) == NULL)
			user = (pw = getpwuid(getuid())) ?
			    pw->pw_name : "???";
		if (dohalt) {
			openlog("halt", LOG_CONS, LOG_AUTH);
			syslog(LOG_CRIT, "halted by %s", user);
		} else {
			openlog("reboot", LOG_CONS, LOG_AUTH);
			syslog(LOG_CRIT, "rebooted by %s", user);
		}
	}
	logwtmpx("~", "shutdown", "", 0, INIT_PROCESS);

	/*
	 * Do a sync early on, so disks start transfers while we're off
	 * killing processes.  Don't worry about writes done before the
	 * processes die, the reboot system call syncs the disks.
	 */
	if (!nflag)
		sync();

	/* Just stop init -- if we fail, we'll restart it. */
	if (kill(1, SIGTSTP) == -1)
		err(1, "SIGTSTP init");

	/* Ignore the SIGHUP we get when our parent shell dies. */
	signal(SIGHUP, SIG_IGN);
	/* parent shell might also send a SIGTERM?  Best to ignore as well */
	signal(SIGTERM, SIG_IGN);
	/* Group leaders may try killing us with other signals, ignore */
	signal(SIGINT,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	/*
	 * If we're running in a pipeline, we don't want to die
	 * after killing whatever we're writing to.
	 */
	signal(SIGPIPE, SIG_IGN);

	/* Send a SIGTERM first, a chance to save the buffers. */
	if (kill(-1, SIGTERM) == -1)
		err(1, "SIGTERM processes");

	/*
	 * After the processes receive the signal, start the rest of the
	 * buffers on their way.  Wait 5 seconds between the SIGTERM and
	 * the SIGKILL to give everybody a chance. If there is a lot of
	 * paging activity then wait longer, up to a maximum of approx
	 * 60 seconds.
	 */
	sleep(2);
	for (i = 0; i < 20; i++) {
		pageins = get_pageins();
		if (!nflag)
			sync();
		sleep(3);
		if (get_pageins() == pageins)
			break;
	}

	for (i = 1;; ++i) {
		if (kill(-1, SIGKILL) == -1) {
			if (errno == ESRCH)
				break;
			goto restart;
		}
		if (i > 5) {
			fprintf(stderr,
			    "WARNING: some process(es) wouldn't die\n");
			break;
		}
		sleep(2 * i);
	}

	reboot(howto);
	/* FALLTHROUGH */

restart:
	sverrno = errno;
	errx(1, "%s%s", kill(1, SIGHUP) == -1 ? "(can't restart init): " : "",
	    strerror(sverrno));
	/* NOTREACHED */
}

static void
usage(void)
{
	if (donextboot)
		fprintf(stderr, "usage: nextboot [-aDf] [-e var=value] "
		    "[-k kernel] [-o options]\n");
	else
		fprintf(stderr, "usage: %s [-Ddflnpq] [-e var=value] "
		    "[-k kernel] [-o options]\n",
		    dohalt ? "halt" : "reboot");
	exit(1);
}

static u_int
get_pageins(void)
{
	u_int pageins;
	size_t len;

	len = sizeof(pageins);
	if (sysctlbyname("vm.stats.vm.v_swappgsin", &pageins, &len, NULL, 0)
	    != 0) {
		warnx("v_swappgsin");
		return (0);
	}
	return pageins;
}
