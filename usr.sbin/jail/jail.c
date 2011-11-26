/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 * 
 * $FreeBSD: src/usr.sbin/jail/jail.c,v 1.5.2.2 2003/05/08 13:04:24 maxim Exp $
 * $DragonFly: src/usr.sbin/jail/jail.c,v 1.8 2007/01/17 10:37:48 victor Exp $
 * 
 */

#include <sys/param.h>
#include <sys/jail.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	usage(void);
extern char	**environ;

#define GET_USER_INFO do {						\
	pwd = getpwnam(username);					\
	if (pwd == NULL) {						\
		if (errno)						\
			err(1, "getpwnam: %s", username);		\
		else							\
			errx(1, "%s: no such user", username);		\
	}								\
	lcap = login_getpwclass(pwd);					\
	if (lcap == NULL)						\
		err(1, "getpwclass: %s", username);			\
	ngroups = NGROUPS;						\
	if (getgrouplist(username, pwd->pw_gid, groups, &ngroups) != 0)	\
		err(1, "getgrouplist: %s", username);			\
} while (0)

int
main(int argc, char **argv)
{
	login_cap_t *lcap = NULL;
	struct jail j;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	struct passwd *pwd = NULL;
	gid_t groups[NGROUPS];
	int ch, ngroups, i, iflag, lflag, uflag, Uflag;
	static char *cleanenv;
	const char *shell, *p = NULL;
	char path[PATH_MAX], *username, *curpos;

	iflag = lflag = uflag = Uflag = 0;
	username = NULL;
	j.ips = malloc(sizeof(struct sockaddr_storage)*20);
	bzero(j.ips, sizeof(struct sockaddr_storage)*20);

	while ((ch = getopt(argc, argv, "ilu:U:")) != -1)
		switch (ch) {
		case 'i':
			iflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'u':
			username = optarg;
			uflag = 1;
			break;
		case 'U':
			username = optarg;
			Uflag = 1;
			break;
		default:
			usage();
			break;
		}
	argc -= optind;
	argv += optind;
	if (argc < 4)
		usage();
	if (uflag && Uflag)
		usage();
	if (lflag && username == NULL)
		usage();
	if (uflag)
		GET_USER_INFO;
	if (realpath(argv[0], path) == NULL)
		err(1, "realpath: %s", argv[0]);
	if (chdir(path) != 0)
		err(1, "chdir: %s", path);

	j.version = 1;
	j.path = path;
	j.hostname = argv[1];
	curpos = strtok(argv[2], ",");
	for (i=0; curpos != NULL; i++) {
		if (i && i%20 == 0) {
			if ( (j.ips = realloc(j.ips, sizeof(struct sockaddr_storage)*i+20)) == NULL) {
				perror("Can't allocate memory");
				exit(1);
			}
		}

		sin4 = (struct sockaddr_in *)(j.ips+i);
		sin6 = (struct sockaddr_in6 *)(j.ips+i);

		if (inet_pton(AF_INET, curpos, &sin4->sin_addr) == 1) {
			sin4->sin_family = AF_INET;
		} else {
			if (inet_pton(AF_INET6, curpos, &sin6->sin6_addr) == 1) {
				sin6->sin6_family = AF_INET6;
			} else {
				printf("Invalid value %s\n", curpos);
				exit(1);
			}
		}
		curpos = strtok(NULL, ",");
	}

	j.n_ips = i; 
	i = jail(&j);
	if (i == -1)
		err(1, "jail");
	if (iflag) {
		printf("%d\n", i);
		fflush(stdout);
	}
	if (username != NULL) {
		if (Uflag)
			GET_USER_INFO;
		if (lflag) {
			p = getenv("TERM");
			environ = &cleanenv;
		}
		if (setgroups(ngroups, groups) != 0)
			err(1, "setgroups");
		if (setgid(pwd->pw_gid) != 0)
			err(1, "setgid");
		if (setusercontext(lcap, pwd, pwd->pw_uid,
		    LOGIN_SETALL & ~LOGIN_SETGROUP) != 0)
			err(1, "setusercontext");
		login_close(lcap);
	}
	if (lflag) {
		if (*pwd->pw_shell)
			shell = pwd->pw_shell;
		else
			shell = _PATH_BSHELL;
		if (chdir(pwd->pw_dir) < 0)
			errx(1, "no home directory");
		setenv("HOME", pwd->pw_dir, 1);
		setenv("SHELL", shell, 1);
		setenv("USER", pwd->pw_name, 1);
		if (p)
			setenv("TERM", p, 1);
	}
	if (execv(argv[3], argv + 3) != 0)
		err(1, "execv: %s", argv[3]);
	exit (0);
}

static void
usage(void)
{

	fprintf(stderr, "%s\n",
	    "Usage: jail [-i] [-l -u username | -U username] path hostname ip-list command ...");
	exit(1);
}
