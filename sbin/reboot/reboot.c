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

#include "nextboot_efi.h"

#define PATH_NEXTBOOT "/boot/nextboot.conf"

#define GETOPT_REBOOT	"Dde:fk:lno:pq"
#define GETOPT_NEXTBOOT	"aDe:fk:o:"

static void usage(void) __dead2;
static u_int get_pageins(void);
static int read_nextboot(const char *, char *, size_t, size_t *, int);
static int nextboot_enabled(const char *);
static int armed_file_status(void);
static char *build_nextboot(const char *, const char *, size_t *);
static void write_nextboot_file(const char *, const char *, size_t, int);
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

static int
read_nextboot(const char *fn, char *buf, size_t bufsz, size_t *lenp,
    int force)
{
	size_t len;
	int c, fd, n;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return (0);
		if (!force)
			err(1, "open %s", fn);
		warn("open %s", fn);
		return (-1);
	}
	len = 0;
	while (len < bufsz) {
		n = read(fd, buf + len, bufsz - len);
		if (n < 0) {
			close(fd);
			if (!force)
				err(1, "read %s", fn);
			warn("read %s", fn);
			return (-1);
		}
		if (n == 0)
			break;
		len += n;
	}
	c = 0;
	if (len == bufsz && read(fd, &c, 1) == 1) {
		close(fd);
		if (!force)
			errx(1, "%s is larger than %d bytes", fn,
			    NEXTBOOT_MAX_SIZE);
		warnx("%s is larger than %d bytes, replacing it", fn,
		    NEXTBOOT_MAX_SIZE);
		return (0);
	}
	close(fd);
	buf[len] = '\0';
	*lenp = len;
	return (1);
}

static int
nv_enable_line(const char *text, size_t len)
{
	const char *end, *eq, *nl, *p, *value;
	size_t nlen;

	p = text;
	while (len > 0 && (*p == ' ' || *p == '\t')) {
		++p;
		--len;
	}
	if (len == 0 || *p == '#')
		return (0);
	end = p + len;
	eq = memchr(p, '=', len);
	if (eq == NULL)
		return (0);
	nlen = eq - p;
	while (nlen > 0 && (p[nlen - 1] == ' ' || p[nlen - 1] == '\t'))
		--nlen;
	if (nlen != 15 || strncmp(p, "nextboot_enable", 15) != 0)
		return (0);
	value = eq + 1;
	while (value < end && (*value == ' ' || *value == '\t'))
		++value;
	nl = memchr(value, '\n', end - value);
	if (nl != NULL)
		end = nl;
	while (end > value && (end[-1] == '\n' || end[-1] == '\r' ||
	    end[-1] == ' ' || end[-1] == '\t'))
		--end;
	nlen = end - value;
	if (*value == '"' && nlen >= 2 && end[-1] == '"') {
		++value;
		nlen -= 2;
	}
	return (nlen == 3 &&
	    (strncmp(value, "YES", 3) == 0 || strncmp(value, "yes", 3) == 0));
}

static int
nextboot_enabled(const char *text)
{
	const char *nl;

	nl = strchr(text, '\n');
	return (nv_enable_line(text, nl != NULL ? (size_t)(nl - text) :
	    strlen(text)));
}

static char *
build_nextboot(const char *old, const char *env, size_t *lenp)
{
	const char *nl, *p;
	char *buf;
	size_t len, linelen;

	if (asprintf(&buf, "nextboot_enable=\"YES\"\n") == -1)
		err(1, NULL);
	len = strlen(buf);

	if (old != NULL && !nextboot_enabled(old))
		old = NULL;
	for (p = old; p != NULL && *p != '\0'; p = nl + 1) {
		nl = strchr(p, '\n');
		if (nl == NULL)
			nl = p + strlen(p);
		linelen = nl - p;
		if (nv_enable_line(p, linelen)) {
			if (*nl == '\0')
				break;
			continue;
		}
		buf = realloc(buf, len + linelen + 2);
		if (buf == NULL)
			err(1, NULL);
		memcpy(buf + len, p, linelen);
		len += linelen;
		buf[len++] = '\n';
		buf[len] = '\0';
		if (*nl == '\0')
			break;
	}
	if (env != NULL) {
		linelen = strlen(env);
		buf = realloc(buf, len + linelen + 1);
		if (buf == NULL)
			err(1, NULL);
		memcpy(buf + len, env, linelen + 1);
		len += linelen;
	}
	*lenp = len;
	return (buf);
}

static int
armed_file_status(void)
{
	static char buf[NEXTBOOT_MAX_SIZE + 1];
	int fd, n;

	fd = open(PATH_NEXTBOOT, O_RDONLY);
	if (fd < 0) {
		n = errno;
		if (n != ENOENT)
			warn("open " PATH_NEXTBOOT);
		return (n == ENOENT ? 0 : -1);
	}
	/* The loader reads this many bytes and asks for the first line. */
	n = read(fd, buf, NEXTBOOT_MAX_SIZE);
	close(fd);
	if (n < 0) {
		warn("read " PATH_NEXTBOOT);
		return (-1);
	}
	buf[n] = '\0';
	return (nextboot_enabled(buf) ? 1 : 0);
}

static void
write_nextboot_file(const char *fn, const char *text, size_t len, int force)
{
	char tmp[PATH_MAX];
	struct statfs sfs;
	FILE *fp;
	int tmpfd;

	if (len > NEXTBOOT_MAX_SIZE)
		errx(1, "%s is larger than %d bytes", fn, NEXTBOOT_MAX_SIZE);

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
	if (fwrite(text, 1, len, fp) != len)
		NEXTBOOT_E("Can't write %s", tmp);
	if (fflush(fp) != 0)
		NEXTBOOT_E("Can't write %s", tmp);
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

static void
write_nextboot(const char *fn, const char *env, int append, int force)
{
	static char old[NEXTBOOT_MAX_SIZE + 1];
	size_t len, oldlen;
	char *text;
	int rv;

	oldlen = 0;
	if (booted_uefi()) {
		if (geteuid() != 0) {
			if (!force)
				errx(1, "must be root to write EFI variables");
			warnx("must be root to write EFI variables");
			return;
		}
		if (!nextboot_backend_known())
			warnx("the loader that started this system does not "
			    "read NextBootConf (kenv nextboot_backend is not "
			    "efivar); the entry will be applied only once "
			    "loader.efi is updated");
		if (append) {
			rv = nextboot_efi_read(old, NEXTBOOT_MAX_SIZE,
			    &oldlen, force);
			if (rv == -1)
				return;
			if (rv != 0)
				oldlen = 0;
		}
		text = build_nextboot(oldlen != 0 ? old : NULL, env, &len);
		if (len > NEXTBOOT_MAX_SIZE)
			errx(1, "the entry is larger than %d bytes",
			    NEXTBOOT_MAX_SIZE);
		if (nextboot_efi_write(text, len, force) == 0) {
			switch (armed_file_status()) {
			case 1:
				if (unlink(PATH_NEXTBOOT) != 0) {
					warn("unlink " PATH_NEXTBOOT);
					warnx("an armed " PATH_NEXTBOOT
					    " remains and would be applied "
					    "by a BIOS boot");
				} else {
					warnx("removed an armed "
					    PATH_NEXTBOOT ", which would "
					    "have been applied by a BIOS "
					    "boot");
				}
				break;
			case -1:
				warnx("if " PATH_NEXTBOOT " is armed, a "
				    "BIOS boot of this disk would apply "
				    "it");
				break;
			}
		}
		free(text);
		return;
	}

	if (append) {
		rv = read_nextboot(fn, old, sizeof(old) - 1, &oldlen, force);
		if (rv == -1)
			return;
	}
	text = build_nextboot(oldlen != 0 ? old : NULL, env, &len);
	write_nextboot_file(fn, text, len, force);
	free(text);
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
		int error = 0;

		if (booted_uefi()) {
			if (geteuid() != 0) {
				warnx("must be root to remove the EFI "
				    "variable; the entry stays in place");
				error = 1;
			} else if (nextboot_efi_clear() != 0) {
				error = 1;
			}
		}
		if (unlink(PATH_NEXTBOOT) != 0 && errno != ENOENT) {
			warn("unlink " PATH_NEXTBOOT);
			error = 1;
		}
		exit(error);
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
