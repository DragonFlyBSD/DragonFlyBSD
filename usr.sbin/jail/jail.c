/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 * 
 * $FreeBSD: src/usr.sbin/jail/jail.c,v 1.5.2.2 2003/05/08 13:04:24 maxim Exp $
 * $DragonFly: src/usr.sbin/jail/jail.c,v 1.5 2006/12/29 18:02:57 victor Exp $
 * 
 */

#include <sys/param.h>
#include <sys/jail.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <grp.h>
#include <login_cap.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	usage(void);

int
main(int argc, char **argv)
{
	login_cap_t *lcap = NULL;
	struct jail j;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	struct passwd *pwd = NULL;
	gid_t groups[NGROUPS];
	int ch, ngroups, i;
	char *username, *curpos;

	username = NULL;
	j.ips = malloc(sizeof(struct sockaddr_storage)*20);
	bzero(j.ips, sizeof(struct sockaddr_storage)*20);

	while ((ch = getopt(argc, argv, "u:")) != -1)
		switch (ch) {
		case 'u':
			username = optarg;
			break;
		default:
			usage();
			break;
		}
	argc -= optind;
	argv += optind;
	if (argc < 4)
		usage();

	if (username != NULL) {
		pwd = getpwnam(username);
		if (pwd == NULL)
			err(1, "getpwnam: %s", username);
		lcap = login_getpwclass(pwd);
		if (lcap == NULL)
			err(1, "getpwclass: %s", username);
		ngroups = NGROUPS;
		if (getgrouplist(username, pwd->pw_gid, groups, &ngroups) != 0)
			err(1, "getgrouplist: %s", username);
	}
	if (chdir(argv[0]) != 0)
		err(1, "chdir: %s", argv[0]);
	j.version = 1;
	j.path = argv[0];
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
	if (jail(&j) != 0)
		err(1, "jail");
	if (username != NULL) {
		if (setgroups(ngroups, groups) != 0)
			err(1, "setgroups");
		if (setgid(pwd->pw_gid) != 0)
			err(1, "setgid");
		if (setusercontext(lcap, pwd, pwd->pw_uid,
		    LOGIN_SETALL & ~LOGIN_SETGROUP) != 0)
			err(1, "setusercontext");
		login_close(lcap);
	}
	if (execv(argv[3], argv + 3) != 0)
		err(1, "execv: %s", argv[3]);
	exit (0);
}

static void
usage(void)
{

	fprintf(stderr, "%s\n",
	    "Usage: jail [-u username] path hostname ip-number command ...");
	exit(1);
}
