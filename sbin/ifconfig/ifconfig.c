/*
 * Copyright (c) 1983, 1993
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
 * $FreeBSD: src/sbin/ifconfig/ifconfig.c,v 1.113.2.4 2006/02/09 10:48:43 yar Exp $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/cdefs.h>
#include <sys/queue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

/* IP */
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ifconfig.h"

/*
 * Since "struct ifreq" is composed of various union members, callers
 * should pay special attention to interpret the value.
 * (.e.g. little/big endian difference in the structure.)
 */
struct	ifreq ifr;

char	name[IFNAMSIZ];
int	setaddr;
int	setmask;
int	doalias;
int	clearaddr;
int	newaddr = 1;
int	verbose;
int	noload;

int	supmedia = 0;
int	printkeys = 0;		/* Print keying material for interfaces. */
int	printifname = 0;	/* Print the name of the created interface. */
int	exit_code = 0;

/* Formatter strings */
char	*f_inet, *f_inet6, *f_ether, *f_addr;

static	bool group_member(const char *ifname, const char *match,
			  const char *nomatch);
static	int ifconfig(int argc, char *const *argv, int iscreate,
		     const struct afswtch *afp);
static	void status(const struct afswtch *afp, const struct sockaddr_dl *sdl,
		    struct ifaddrs *ifa);
static	void tunnel_status(int s);
static	void usage(void) __dead2;

static int getifflags(const char *ifname, int us);
static struct afswtch *af_getbyname(const char *name);
static struct afswtch *af_getbyfamily(int af);
static void af_other_status(int);
static void printifnamemaybe(void);
static void freeformat(void);
static void setformat(char *input);

static struct option *opts = NULL;

struct ifa_order_elt {
	int		if_order;
	int		af_orders[255];
	struct ifaddrs	*ifa;
	TAILQ_ENTRY(ifa_order_elt) link;
};
TAILQ_HEAD(ifa_queue, ifa_order_elt);

static int calcorders(struct ifaddrs *ifa, struct ifa_queue *q);
static int cmpifaddrs(struct ifaddrs *a, struct ifaddrs *b,
		      struct ifa_queue *q);
typedef int (*ifaddrs_cmp)(struct ifaddrs *, struct ifaddrs *, struct ifa_queue *);
static struct ifaddrs *sortifaddrs(struct ifaddrs *list, ifaddrs_cmp compare,
				   struct ifa_queue *q);


void
opt_register(struct option *p)
{
	p->next = opts;
	opts = p;
}

static void
usage(void)
{
	char options[1024];
	struct option *p;

	/* XXX not right but close enough for now */
	options[0] = '\0';
	for (p = opts; p != NULL; p = p->next) {
		strlcat(options, p->opt_usage, sizeof(options));
		strlcat(options, " ", sizeof(options));
	}

	fprintf(stderr,
	"usage: ifconfig %s[-n] [-f type:format] interface address_family\n"
	"                [address [dest_address]] [parameters]\n"
	"       ifconfig [-n] interface create\n"
	"       ifconfig [-n] interface destroy\n"
	"       ifconfig -a %s[-G nogroup] [-d | -u] [-m] [-v] [address_family]\n"
	"       ifconfig -l [-d | -u] [address_family]\n"
	"       ifconfig %s[-d | -u] [-m] [-v]\n",
		options, options, options);
	exit(1);
}

static int
calcorders(struct ifaddrs *ifa, struct ifa_queue *q)
{
	struct ifaddrs *prev;
	struct ifa_order_elt *cur;
	unsigned int ord, af, ifa_ord;

	prev = NULL;
	cur = NULL;
	ord = 0;
	ifa_ord = 0;

	while (ifa != NULL) {
		if (prev == NULL ||
		    strcmp(ifa->ifa_name, prev->ifa_name) != 0) {
			cur = calloc(1, sizeof(*cur));
			if (cur == NULL)
				return (-1);

			TAILQ_INSERT_TAIL(q, cur, link);
			cur->if_order = ifa_ord++;
			cur->ifa = ifa;
			ord = 0;
		}

		if (ifa->ifa_addr) {
			af = ifa->ifa_addr->sa_family;

			if (af < nitems(cur->af_orders) &&
			    cur->af_orders[af] == 0)
				cur->af_orders[af] = ++ord;
		}

		prev = ifa;
		ifa = ifa->ifa_next;
	}

	return (0);
}

static int
cmpifaddrs(struct ifaddrs *a, struct ifaddrs *b, struct ifa_queue *q)
{
	struct ifa_order_elt *cur, *e1, *e2;
	unsigned int af1, af2;

	e1 = e2 = NULL;

	if (strcmp(a->ifa_name, b->ifa_name) != 0) {
		TAILQ_FOREACH(cur, q, link) {
			if (e1 != NULL && e2 != NULL)
				break;

			if (strcmp(cur->ifa->ifa_name, a->ifa_name) == 0)
				e1 = cur;
			else if (strcmp(cur->ifa->ifa_name, b->ifa_name) == 0)
				e2 = cur;
		}

		if (e1 == NULL || e2 == NULL)
			return (0);
		else
			return (e1->if_order - e2->if_order);

	} else if (a->ifa_addr != NULL && b->ifa_addr != NULL) {
		TAILQ_FOREACH(cur, q, link) {
			if (strcmp(cur->ifa->ifa_name, a->ifa_name) == 0) {
				e1 = cur;
				break;
			}
		}

		if (e1 == NULL)
			return (0);

		af1 = a->ifa_addr->sa_family;
		af2 = b->ifa_addr->sa_family;

		if (af1 < nitems(e1->af_orders) && af2 < nitems(e1->af_orders))
			return (e1->af_orders[af1] - e1->af_orders[af2]);
	}

	return (0);
}

static struct ifaddrs *
sortifaddrs(struct ifaddrs *list, ifaddrs_cmp compare, struct ifa_queue *q)
{
	struct ifaddrs *right, *temp, *last, *result, *next, *tail;
	
	right = temp = last = list;
	result = next = tail = NULL;

	if (list == NULL || list->ifa_next == NULL)
		return (list);

	while (temp != NULL && temp->ifa_next != NULL) {
		last = right;
		right = right->ifa_next;
		temp = temp->ifa_next->ifa_next;
	}

	last->ifa_next = NULL;

	list = sortifaddrs(list, compare, q);
	right = sortifaddrs(right, compare, q);

	while (list != NULL || right != NULL) {
		if (right == NULL) {
			next = list;
			list = list->ifa_next;
		} else if (list == NULL) {
			next = right;
			right = right->ifa_next;
		} else if (compare(list, right, q) <= 0) {
			next = list;
			list = list->ifa_next;
		} else {
			next = right;
			right = right->ifa_next;
		}

		if (result == NULL)
			result = next;
		else
			tail->ifa_next = next;

		tail = next;
	}

	return (result);
}

static void
printifnamemaybe(void)
{
	if (printifname)
		printf("%s\n", name);
}

static void
freeformat(void)
{
	if (f_inet != NULL)
		free(f_inet);
	if (f_inet6 != NULL)
		free(f_inet6);
	if (f_ether != NULL)
		free(f_ether);
	if (f_addr != NULL)
		free(f_addr);
}

static void
setformat(char *input)
{
	char *formatstr, *category, *modifier;
	char **fp;

	formatstr = strdup(input);
	if (formatstr == NULL)
		err(1, "no memory to set format");

	while ((category = strsep(&formatstr, ",")) != NULL) {
		modifier = strchr(category, ':');
		if (modifier == NULL || modifier[1] == '\0') {
			warnx("skip invalid format specification: %s\n",
			      category);
			continue;
		}

		modifier[0] = '\0';
		modifier++;

		fp = NULL;
		if (strcmp(category, "addr") == 0)
			fp = &f_addr;
		else if (strcmp(category, "ether") == 0)
			fp = &f_ether;
		else if (strcmp(category, "inet") == 0)
			fp = &f_inet;
		else if (strcmp(category, "inet6") == 0)
			fp = &f_inet6;

		if (fp != NULL) {
			*fp = strdup(modifier);
			if (*fp == NULL)
				err(1, "strdup");
		}
	}

	free(formatstr);
}


int
main(int argc, char *argv[])
{
	int c, all, namesonly, downonly, uponly;
	int ifindex, flags;
	const struct afswtch *afp = NULL;
	const struct sockaddr_dl *sdl;
	const char *ifname, *matchgroup, *nogroup;
	struct ifa_order_elt *cur, *tmp;
	struct ifa_queue q = TAILQ_HEAD_INITIALIZER(q);
	struct ifaddrs *ifap, *sifap, *ifa;
	struct ifreq paifr;
	struct option *p;
	size_t iflen;
	char *envformat, *cp;
	char options[1024];

	all = downonly = uponly = namesonly = verbose = noload = 0;
	f_inet = f_inet6 = f_ether = f_addr = NULL;
	matchgroup = nogroup = NULL;

	/*
	 * Ensure we print interface name when expected to,
	 * even if we terminate early due to error.
	 */
	atexit(printifnamemaybe);

	envformat = getenv("IFCONFIG_FORMAT");
	if (envformat != NULL)
		setformat(envformat);

	/* Parse leading line options */
	strlcpy(options, "adf:G:klmnuv", sizeof(options));
	for (p = opts; p != NULL; p = p->next)
		strlcat(options, p->opt, sizeof(options));
	while ((c = getopt(argc, argv, options)) != -1) {
		switch (c) {
		case 'a':	/* scan all interfaces */
			all++;
			break;
		case 'd':	/* restrict scan to "down" interfaces */
			downonly++;
			break;
		case 'f':
			setformat(optarg);
			break;
		case 'G':
			if (!all)
				usage();
			nogroup = optarg;
			break;
		case 'k':
			printkeys++;
			break;
		case 'l':	/* scan interface names only */
			namesonly++;
			break;
		case 'm':	/* show media choices in status */
			supmedia = 1;
			break;
		case 'n':	/* suppress module loading */
			noload++;
			break;
		case 'u':	/* restrict scan to "up" interfaces */
			uponly++;
			break;
		case 'v':
			verbose++;
			break;
		case 'g':
			if (all) {
				matchgroup = optarg;
				break;
			}
			/* FALLTHROUGH (for ifgroup) */
		default:
			for (p = opts; p != NULL; p = p->next)
				if (p->opt[0] == c) {
					p->cb(optarg);
					break;
				}
			if (p == NULL)
				usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	/* -l cannot be used with -a or -m */
	if (namesonly && (all || supmedia))
		usage();

	/* nonsense.. */
	if (uponly && downonly)
		usage();

	/* no arguments is equivalent to '-a' */
	if (!namesonly && argc < 1)
		all = 1;

	/* -a and -l allow an address family arg to limit the output */
	if (all || namesonly) {
		if (argc > 1)
			usage();

		ifname = NULL;
		ifindex = 0;
		if (argc == 1) {
			afp = af_getbyname(*argv);
			if (afp == NULL)
				usage();
			if (afp->af_name != NULL)
				argc--, argv++;
			/* leave with afp non-zero */
		}
	} else {
		/* not listing, need an argument */
		if (argc < 1)
			usage();

		ifname = *argv;
		argc--, argv++;

		/* check and maybe load support for this interface */
		ifmaybeload(ifname);

		ifindex = if_nametoindex(ifname);
		if (ifindex == 0) {
			/*
			 * NOTE:  We must special-case the `create' command
			 * right here as we would otherwise fail when trying
			 * to find the interface.
			 */
			if (argc > 0 && (strcmp(argv[0], "create") == 0 ||
			    strcmp(argv[0], "plumb") == 0)) {
				iflen = strlcpy(name, ifname, sizeof(name));
				if (iflen >= sizeof(name))
					errx(1, "%s: cloning name too long",
					    ifname);
				ifconfig(argc, argv, 1, NULL);
				exit(exit_code);
			}
			errx(1, "interface %s does not exist", ifname);
		} else {
			/*
			 * Do not allow to use `create` command as hostname
			 * if address family is not specified.
			 */
			if (argc > 0 && (strcmp(argv[0], "create") == 0 ||
			    strcmp(argv[0], "plumb") == 0)) {
				if (argc == 1)
					errx(1, "interface %s already exists",
					    ifname);
				argc--, argv++;
			}
		}
	}

	/* Check for address family */
	if (argc > 0) {
		afp = af_getbyname(*argv);
		if (afp != NULL)
			argc--, argv++;
	}

	/*
	 * Check for a requested configuration action on a single interface,
	 * which doesn't require building, sorting, and searching the entire
	 * system address list.
	 */
	if (argc > 0 && ifname != NULL) {
		iflen = strlcpy(name, ifname, sizeof(name));
		if (iflen >= sizeof(name))
			errx(1, "%s: cloning name too long", ifname);

		flags = getifflags(name, -1);
		if (!((downonly && (flags & IFF_UP) != 0) ||
		      (uponly && (flags & IFF_UP) == 0))) {
			ifconfig(argc, argv, 0, afp);
		}

		exit(exit_code);
	}

	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");
	if (calcorders(ifap, &q) != 0)
		err(1, "calcorders");
	sifap = sortifaddrs(ifap, cmpifaddrs, &q);

	TAILQ_FOREACH_MUTABLE(cur, &q, link, tmp)
		free(cur);

	cp = NULL;
	ifindex = 0;
	for (ifa = sifap; ifa != NULL; ifa = ifa->ifa_next) {
		memset(&paifr, 0, sizeof(paifr));
		strlcpy(paifr.ifr_name, ifa->ifa_name, sizeof(paifr.ifr_name));
		if (sizeof(paifr.ifr_addr) >= ifa->ifa_addr->sa_len) {
			memcpy(&paifr.ifr_addr, ifa->ifa_addr,
			       ifa->ifa_addr->sa_len);
		}

		if (ifname != NULL && strcmp(ifname, ifa->ifa_name) != 0)
			continue;
		if (cp != NULL && strcmp(cp, ifa->ifa_name) == 0)
			continue;
		iflen = strlcpy(name, ifa->ifa_name, sizeof(name));
		if (iflen >= sizeof(name)) {
			warnx("%s: interface name too long, skipping",
			      ifa->ifa_name);
			continue;
		}
		cp = ifa->ifa_name;

		if (downonly && (ifa->ifa_flags & IFF_UP) != 0)
			continue;
		if (uponly && (ifa->ifa_flags & IFF_UP) == 0)
			continue;
		if (!group_member(ifa->ifa_name, matchgroup, nogroup))
			continue;

		if (ifa->ifa_addr->sa_family == AF_LINK)
			sdl = (const struct sockaddr_dl *)ifa->ifa_addr;
		else
			sdl = NULL;

		/* Are we just listing the interfaces? */
		if (namesonly) {
			if (afp == NULL ||
			    afp->af_af != AF_LINK ||
			    (sdl != NULL && sdl->sdl_type == IFT_ETHER)) {
				printf("%s%s", (ifindex > 0 ? " " : ""), name);
				ifindex++;
			}
			continue;
		}

		if (argc > 0)
			ifconfig(argc, argv, 0, afp);
		else
			status(afp, sdl, ifa);
	}

	if (namesonly)
		putchar('\n');

	freeifaddrs(ifap);
	freeformat();

	return (exit_code);
}


/*
 * Returns true if an interface should be listed because any its groups
 * matches shell pattern "match" and none of groups matches pattern "nomatch".
 * If any pattern is NULL, corresponding condition is skipped.
 */
static bool
group_member(const char *ifname, const char *match, const char *nomatch)
{
	static int		 sock = -1;

	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	size_t			 len;
	bool			 matched, nomatched;

	/* Sanity checks. */
	if (match == NULL && nomatch == NULL)
		return (true);
	if (ifname == NULL)
		return (false);

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifname, sizeof(ifgr.ifgr_name));

	/* The socket is opened once. Let _exit() close it. */
	if (sock == -1) {
		sock = socket(AF_LOCAL, SOCK_DGRAM, 0);
		if (sock == -1)
			errx(1, "%s: socket(AF_LOCAL,SOCK_DGRAM)", __func__);
	}

	/* Determine amount of memory for the list of groups. */
	if (ioctl(sock, SIOCGIFGROUP, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY)
			return (false);
		else
			errx(1, "%s: SIOCGIFGROUP", __func__);
	}

	/* Obtain the list of groups. */
	len = ifgr.ifgr_len;
	ifgr.ifgr_groups =
		(struct ifg_req *)calloc(len / sizeof(*ifg), sizeof(*ifg));
	if (ifgr.ifgr_groups == NULL)
		errx(1, "%s: no memory", __func__);
	if (ioctl(sock, SIOCGIFGROUP, (caddr_t)&ifgr) == -1)
		errx(1, "%s: SIOCGIFGROUP", __func__);

	/* Perform matching. */
	matched = false;
	nomatched = true;
	for (ifg = ifgr.ifgr_groups; ifg && len >= sizeof(*ifg); ifg++) {
		len -= sizeof(struct ifg_req);
		if (match)
			matched |= !fnmatch(match, ifg->ifgrq_group, 0);
		if (nomatch)
			nomatched &= fnmatch(nomatch, ifg->ifgrq_group, 0);
	}

	if (match && !nomatch)
		return (matched);
	if (!match && nomatch)
		return (nomatched);
	return (matched && nomatched);
}


static struct afswtch *afs = NULL;

void
af_register(struct afswtch *p)
{
	p->af_next = afs;
	afs = p;
}

static struct afswtch *
af_getbyname(const char *name)
{
	struct afswtch *afp;

	for (afp = afs; afp !=  NULL; afp = afp->af_next)
		if (strcmp(afp->af_name, name) == 0)
			return afp;
	return NULL;
}

static struct afswtch *
af_getbyfamily(int af)
{
	struct afswtch *afp;

	for (afp = afs; afp != NULL; afp = afp->af_next)
		if (afp->af_af == af)
			return afp;
	return NULL;
}

static void
af_other_status(int s)
{
	struct afswtch *afp;
	uint8_t afmask[howmany(AF_MAX, NBBY)];

	memset(afmask, 0, sizeof(afmask));
	for (afp = afs; afp != NULL; afp = afp->af_next) {
		if (afp->af_other_status == NULL)
			continue;
		if (afp->af_af != AF_UNSPEC && isset(afmask, afp->af_af))
			continue;
		afp->af_other_status(s);
		setbit(afmask, afp->af_af);
	}
}

static void
af_all_tunnel_status(int s)
{
	struct afswtch *afp;
	uint8_t afmask[howmany(AF_MAX, NBBY)];

	memset(afmask, 0, sizeof(afmask));
	for (afp = afs; afp != NULL; afp = afp->af_next) {
		if (afp->af_status_tunnel == NULL)
			continue;
		if (afp->af_af != AF_UNSPEC && isset(afmask, afp->af_af))
			continue;
		afp->af_status_tunnel(s);
		setbit(afmask, afp->af_af);
	}
}

static struct cmd *cmds = NULL;

void
cmd_register(struct cmd *p)
{
	p->c_next = cmds;
	cmds = p;
}

static const struct cmd *
cmd_lookup(const char *name, int iscreate)
{
	const struct cmd *p;

	for (p = cmds; p != NULL; p = p->c_next) {
		if (strcmp(name, p->c_name) == 0) {
			if (iscreate) {
				if (p->c_iscloneop)
					return p;
			} else {
				if (!p->c_iscloneop)
					return p;
			}
		}
	}

	return NULL;
}

struct callback {
	callback_func *cb_func;
	void	*cb_arg;
	struct callback *cb_next;
};
static struct callback *callbacks = NULL;

void
callback_register(callback_func *func, void *arg)
{
	struct callback *cb;

	cb = malloc(sizeof(struct callback));
	if (cb == NULL)
		errx(1, "unable to allocate memory for callback");
	cb->cb_func = func;
	cb->cb_arg = arg;
	cb->cb_next = callbacks;
	callbacks = cb;
}

/* specially-handled commands */
static void setifaddr(const char *, int, int, const struct afswtch *);
static const struct cmd setifaddr_cmd = DEF_CMD("ifaddr", 0, setifaddr);

static void setifdstaddr(const char *, int, int, const struct afswtch *);
static const struct cmd setifdstaddr_cmd =
	DEF_CMD("ifdstaddr", 0, setifdstaddr);

static int
ifconfig(int argc, char *const *argv, int iscreate,
	 const struct afswtch *uafp)
{
	const struct afswtch *afp, *nafp;
	struct callback *cb;
	int s;

	strlcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	afp = uafp != NULL ? uafp : af_getbyname("inet");
top:
	ifr.ifr_addr.sa_family =
		afp->af_af == AF_LINK || afp->af_af == AF_UNSPEC ?
		AF_LOCAL : afp->af_af;

	if ((s = socket(ifr.ifr_addr.sa_family, SOCK_DGRAM, 0)) < 0 &&
	    (uafp != NULL || errno != EAFNOSUPPORT ||
	     (s = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0))
		err(1, "socket(family %u,SOCK_DGRAM)", ifr.ifr_addr.sa_family);

	while (argc > 0) {
		const struct cmd *p;

		p = cmd_lookup(*argv, iscreate);

		if (iscreate && p == NULL) {
			/*
			 * Push the clone create callback so the new
			 * device is created and can be used for any
			 * remaining arguments.
			 */
			cb = callbacks;
			if (cb == NULL)
				errx(1, "internal error, no callback");
			callbacks = cb->cb_next;
			cb->cb_func(s, cb->cb_arg);
			iscreate = 0;

			/*
			 * After cloning, make sure we have an up-to-date name
			 * in ifr_name.
			 */
			strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

			/*
			 * Handle any address family spec that
			 * immediately follows and potentially
			 * recreate the socket.
			 */
			nafp = af_getbyname(*argv);
			if (nafp != NULL) {
				argc--, argv++;
				if (nafp != afp) {
					close(s);
					afp = nafp;
					goto top;
				}
			}
			/*
			 * Look for a normal parameter.
			 */
			continue;
		}
		if (p == NULL) {
			/*
			 * Not a recognized command, choose between setting
			 * the interface address and the dst address.
			 */
			p = (setaddr ? &setifdstaddr_cmd : &setifaddr_cmd);
		}
		if (p->c_u.c_func || p->c_u.c_func2) {
			if (p->c_parameter == NEXTARG) {
				if (argv[1] == NULL)
					errx(1, "'%s' requires argument",
					    p->c_name);
				p->c_u.c_func(argv[1], 0, s, afp);
				argc--, argv++;
			} else if (p->c_parameter == OPTARG) {
				p->c_u.c_func(argv[1], 0, s, afp);
				if (argv[1] != NULL)
					argc--, argv++;
			} else if (p->c_parameter == NEXTARG2) {
				if (argc < 3)
					errx(1, "'%s' requires 2 arguments",
					    p->c_name);
				p->c_u.c_func2(argv[1], argv[2], s, afp);
				argc -= 2, argv += 2;
			} else
				p->c_u.c_func(*argv, p->c_parameter, s, afp);
		}
		argc--, argv++;
	}

	/*
	 * Do any post argument processing required by the address family.
	 */
	if (afp->af_postproc != NULL)
		afp->af_postproc(s, afp);
	/*
	 * Do deferred callbacks registered while processing
	 * command-line arguments.
	 */
	for (cb = callbacks; cb != NULL; cb = cb->cb_next)
		cb->cb_func(s, cb->cb_arg);
	/*
	 * Do deferred operations.
	 */
	if (clearaddr) {
		if (afp->af_ridreq == NULL || afp->af_difaddr == 0) {
			warnx("interface %s cannot change %s addresses!",
			      name, afp->af_name);
			clearaddr = 0;
		}
	}
	if (clearaddr) {
		int ret;
		strlcpy(afp->af_ridreq, name, sizeof ifr.ifr_name);
		ret = ioctl(s, afp->af_difaddr, afp->af_ridreq);
		if (ret < 0) {
			if (errno == EADDRNOTAVAIL && (doalias >= 0)) {
				/* means no previous address for interface */
			} else
				Perror("ioctl (SIOCDIFADDR)");
		}
	}
	if (newaddr) {
		if (afp->af_addreq == NULL || afp->af_aifaddr == 0) {
			warnx("interface %s cannot change %s addresses!",
			      name, afp->af_name);
			newaddr = 0;
		}
	}
	if (newaddr && (setaddr || setmask)) {
		strlcpy(afp->af_addreq, name, sizeof ifr.ifr_name);
		if (ioctl(s, afp->af_aifaddr, afp->af_addreq) < 0)
			Perror("ioctl (SIOCAIFADDR)");
	}

	close(s);
	return (0);
}

/*ARGSUSED*/
static void
setifaddr(const char *addr, int param, int s, const struct afswtch *afp)
{
	if (afp->af_getaddr == NULL)
		return;
	/*
	 * Delay the ioctl to set the interface addr until flags are all set.
	 * The address interpretation may depend on the flags,
	 * and the flags may change when the address is set.
	 */
	setaddr++;
	if (doalias == 0 && afp->af_af != AF_LINK)
		clearaddr = 1;
	afp->af_getaddr(addr, (doalias >= 0 ? ADDR : RIDADDR));
}

static void
settunnel(const char *src, const char *dst, int s, const struct afswtch *afp)
{
	struct addrinfo *srcres, *dstres;
	int ecode;

	if (afp->af_settunnel == NULL) {
		warn("address family %s does not support tunnel setup",
			afp->af_name);
		return;
	}

	if ((ecode = getaddrinfo(src, NULL, NULL, &srcres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if ((ecode = getaddrinfo(dst, NULL, NULL, &dstres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if (srcres->ai_addr->sa_family != dstres->ai_addr->sa_family)
		errx(1,
		    "source and destination address families do not match");

	afp->af_settunnel(s, srcres, dstres);

	freeaddrinfo(srcres);
	freeaddrinfo(dstres);
}

/* ARGSUSED */
static void
deletetunnel(const char *vname, int param, int s, const struct afswtch *afp)
{

	if (ioctl(s, SIOCDIFPHYADDR, &ifr) < 0)
		err(1, "SIOCDIFPHYADDR");
}

static void
setifnetmask(const char *addr, int dummy __unused, int s,
    const struct afswtch *afp)
{
	if (afp->af_getaddr != NULL) {
		setmask++;
		afp->af_getaddr(addr, MASK);
	}
}

static void
setifbroadaddr(const char *addr, int dummy __unused, int s,
    const struct afswtch *afp)
{
	if (afp->af_getaddr != NULL)
		afp->af_getaddr(addr, DSTADDR);
}

static void
notealias(const char *addr, int param, int s, const struct afswtch *afp)
{
#define rqtosa(x) (&(((struct ifreq *)(afp->x))->ifr_addr))
	if (setaddr && doalias == 0 && param < 0)
		if (afp->af_addreq != NULL && afp->af_ridreq != NULL)
			bcopy((caddr_t)rqtosa(af_addreq),
			      (caddr_t)rqtosa(af_ridreq),
			      rqtosa(af_addreq)->sa_len);
	doalias = param;
	if (param < 0) {
		clearaddr = 1;
		newaddr = 0;
	} else {
		clearaddr = 0;
	}
#undef rqtosa
}

/*ARGSUSED*/
static void
setifdstaddr(const char *addr, int param __unused, int s,
    const struct afswtch *afp)
{
	if (afp->af_getaddr != NULL)
		afp->af_getaddr(addr, DSTADDR);
}

static int
getifflags(const char *ifname, int us)
{
	struct ifreq my_ifr;
	int s;

	memset(&my_ifr, 0, sizeof(struct ifreq));
	strlcpy(my_ifr.ifr_name, ifname, sizeof(my_ifr.ifr_name));

	s = us;
	if (us < 0) {
		if ((s = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0)
			err(1, "socket(family AF_LOCAL,SOCK_DGRAM)");
	}

	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&my_ifr) < 0)
		Perror("ioctl (SIOCGIFFLAGS)");

	if (us < 0)
		close(s);

	return ((my_ifr.ifr_flags & 0xffff) | (my_ifr.ifr_flagshigh << 16));
}

static void
setifflags(const char *vname, int value, int s, const struct afswtch *afp)
{
	struct ifreq my_ifr;
	int flags;

	flags = getifflags(name, s);
	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else {
		flags |= value;
	}

	memset(&my_ifr, 0, sizeof(struct ifreq));
	strlcpy(my_ifr.ifr_name, name, sizeof(my_ifr.ifr_name));
	my_ifr.ifr_flags = flags & 0xffff;
	my_ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(s, SIOCSIFFLAGS, (caddr_t)&my_ifr) < 0)
		Perror(vname);
}

void
setifcap(const char *vname, int value, int s, const struct afswtch *afp)
{
	int flags;

	if (ioctl(s, SIOCGIFCAP, (caddr_t)&ifr) < 0)
		Perror("ioctl (SIOCGIFCAP)");

	flags = ifr.ifr_curcap;
	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else {
		flags |= value;
	}
	ifr.ifr_reqcap = flags;
	if (ioctl(s, SIOCSIFCAP, (caddr_t)&ifr) < 0)
		Perror(vname);
}

static void
setifmetric(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_metric = atoi(val);
	if (ioctl(s, SIOCSIFMETRIC, (caddr_t)&ifr) < 0)
		err(1, "ioctl SIOCSIFMETRIC (set metric)");
}

static void
setifmtu(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_mtu = atoi(val);
	if (ioctl(s, SIOCSIFMTU, (caddr_t)&ifr) < 0)
		err(1, "ioctl SIOCSIFMTU (set mtu)");
}

static void
setiftsolen(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_tsolen = atoi(val);
	if (ioctl(s, SIOCSIFTSOLEN, (caddr_t)&ifr) < 0)
		err(1, "ioctl SIOCSIFTSOLEN (set tsolen)");
}

static void
setifname(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	char *newname;

	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	newname = strdup(val);
	if (newname == NULL)
		err(1, "no memory to set ifname");
	ifr.ifr_data = newname;
	if (ioctl(s, SIOCSIFNAME, (caddr_t)&ifr) < 0) {
		free(newname);
		err(1, "ioctl SIOCSIFNAME (set name)");
	}
	printifname = 1;
	strlcpy(name, newname, sizeof(name));
	free(newname);
}

static void
setifpollcpu(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	warnx("pollcpu is deprecated, use polling or npolling instead");
	setifflags("npolling", IFF_NPOLLING, s, afp);
}


#define	IFFBITS \
"\020\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5POINTOPOINT\6SMART\7RUNNING" \
"\10NOARP\11PROMISC\12ALLMULTI\14SIMPLEX\15LINK0\16LINK1\17LINK2" \
"\20MULTICAST\22PPROMISC\23MONITOR\24STATICARP\25NPOLLING\26IDIRECT"

#define	IFCAPBITS \
"\020\1RXCSUM\2TXCSUM\3NETCONS\4VLAN_MTU\5VLAN_HWTAGGING\6JUMBO_MTU\7RSS" \
"\10VLAN_HWCSUM\11TSO"

/*
 * Print the status of the interface.  If an address family was
 * specified, show only it; otherwise, show them all.
 */
static void
status(const struct afswtch *afp, const struct sockaddr_dl *sdl,
       struct ifaddrs *ifa)
{
	struct ifaddrs *ift;
	int allfamilies, s;
	struct ifstat ifs;

	if (afp == NULL) {
		allfamilies = 1;
		ifr.ifr_addr.sa_family = AF_LOCAL;
	} else {
		allfamilies = 0;
		ifr.ifr_addr.sa_family =
		    afp->af_af == AF_LINK ? AF_LOCAL : afp->af_af;
	}
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	s = socket(ifr.ifr_addr.sa_family, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket(family %u,SOCK_DGRAM)", ifr.ifr_addr.sa_family);

	printf("%s: ", name);
	printb("flags", ifa->ifa_flags, IFFBITS);
	if (ioctl(s, SIOCGIFMETRIC, &ifr) != -1)
		printf(" metric %d", ifr.ifr_metric);
	if (ioctl(s, SIOCGIFMTU, &ifr) != -1)
		printf(" mtu %d", ifr.ifr_mtu);
	putchar('\n');

	if (ioctl(s, SIOCGIFCAP, (caddr_t)&ifr) == 0) {
		if (ifr.ifr_curcap != 0) {
			printb("\toptions", ifr.ifr_curcap, IFCAPBITS);
			putchar('\n');
		}
		if (supmedia && ifr.ifr_reqcap != 0) {
			printb("\tcapabilities", ifr.ifr_reqcap, IFCAPBITS);
			putchar('\n');
			if (ifr.ifr_reqcap & IFCAP_TSO) {
				if (ioctl(s, SIOCGIFTSOLEN,
				    (caddr_t)&ifr) == 0) {
					printf("\ttsolen %d", ifr.ifr_tsolen);
					putchar('\n');
				}
			}
		}
	}

	tunnel_status(s);

	for (ift = ifa; ift != NULL; ift = ift->ifa_next) {
		if (ift->ifa_addr == NULL)
			continue;
		if (strcmp(ifa->ifa_name, ift->ifa_name) != 0)
			continue;
		if (allfamilies) {
			const struct afswtch *p;
			p = af_getbyfamily(ift->ifa_addr->sa_family);
			if (p != NULL && p->af_status != NULL)
				p->af_status(s, ift);
		} else if (afp->af_af == ift->ifa_addr->sa_family)
			afp->af_status(s, ift);
	}
#if 0
	if (allfamilies || afp->af_af == AF_LINK) {
		const struct afswtch *lafp;

		/*
		 * Hack; the link level address is received separately
		 * from the routing information so any address is not
		 * handled above.  Cobble together an entry and invoke
		 * the status method specially.
		 */
		lafp = af_getbyname("lladdr");
		if (lafp != NULL) {
			info.rti_info[RTAX_IFA] = (struct sockaddr *)sdl;
			lafp->af_status(s, &info);
		}
	}
#endif
	if (allfamilies)
		af_other_status(s);
	else if (afp->af_other_status != NULL)
		afp->af_other_status(s);

	strlcpy(ifs.ifs_name, name, sizeof ifs.ifs_name);
	if (ioctl(s, SIOCGIFSTATUS, &ifs) == 0)
		printf("%s", ifs.ascii);

	close(s);
	return;
}

static void
tunnel_status(int s)
{
	af_all_tunnel_status(s);
}

void
Perror(const char *cmd)
{
	switch (errno) {

	case ENXIO:
		errx(1, "%s: no such interface", cmd);
		break;

	case EPERM:
		errx(1, "%s: permission denied", cmd);
		break;

	default:
		err(1, "%s", cmd);
	}
}

/*
 * Print a value a la the %pb%i format of the kernel's kprintf()
 */
void
printb(const char *s, unsigned v, const char *bits)
{
	int i, any = 0;
	char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);
	bits++;
	if (bits) {
		putchar('<');
		while ((i = *bits++) != '\0') {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

void
ifmaybeload(const char *name)
{
#define MOD_PREFIX_LEN		3	/* "if_" */
	struct module_stat mstat;
	int fileid, modid;
	char ifkind[IFNAMSIZ + MOD_PREFIX_LEN], ifname[IFNAMSIZ], *dp;
	const char *cp;

	/* loading suppressed by the user */
	if (noload)
		return;

	/* trim the interface number off the end */
	strlcpy(ifname, name, sizeof(ifname));
	for (dp = ifname; *dp != 0; dp++)
		if (isdigit(*dp)) {
			*dp = 0;
			break;
		}

	/* turn interface and unit into module name */
	strlcpy(ifkind, "if_", sizeof(ifkind));
	strlcat(ifkind, ifname, sizeof(ifkind));

	/* scan files in kernel */
	mstat.version = sizeof(struct module_stat);
	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid)) {
		/* scan modules in file */
		for (modid = kldfirstmod(fileid); modid > 0;
		     modid = modfnext(modid)) {
			if (modstat(modid, &mstat) < 0)
				continue;
			/* strip bus name if present */
			if ((cp = strchr(mstat.name, '/')) != NULL) {
				cp++;
			} else {
				cp = mstat.name;
			}
			/* already loaded? */
			if (strcmp(ifname, cp) == 0 ||
			    strcmp(ifkind, cp) == 0)
				return;
		}
	}

	/* not present, we should try to load it */
	kldload(ifkind);
}

static struct cmd basic_cmds[] = {
	DEF_CMD("up",		IFF_UP,		setifflags),
	DEF_CMD("down",		-IFF_UP,	setifflags),
	DEF_CMD("arp",		-IFF_NOARP,	setifflags),
	DEF_CMD("-arp",		IFF_NOARP,	setifflags),
	DEF_CMD("debug",	IFF_DEBUG,	setifflags),
	DEF_CMD("-debug",	-IFF_DEBUG,	setifflags),
	DEF_CMD("promisc",	IFF_PPROMISC,	setifflags),
	DEF_CMD("-promisc",	-IFF_PPROMISC,	setifflags),
	DEF_CMD("add",		IFF_UP,		notealias),
	DEF_CMD("alias",	IFF_UP,		notealias),
	DEF_CMD("-alias",	-IFF_UP,	notealias),
	DEF_CMD("delete",	-IFF_UP,	notealias),
	DEF_CMD("remove",	-IFF_UP,	notealias),
#ifdef notdef
#define	EN_SWABIPS	0x1000
	DEF_CMD("swabips",	EN_SWABIPS,	setifflags),
	DEF_CMD("-swabips",	-EN_SWABIPS,	setifflags),
#endif
	DEF_CMD_ARG("netmask",			setifnetmask),
	DEF_CMD_ARG("metric",			setifmetric),
	DEF_CMD_ARG("broadcast",		setifbroadaddr),
	DEF_CMD_ARG2("tunnel",			settunnel),
	DEF_CMD("-tunnel", 0,			deletetunnel),
	DEF_CMD("deletetunnel", 0,		deletetunnel),
	DEF_CMD("link0",	IFF_LINK0,	setifflags),
	DEF_CMD("-link0",	-IFF_LINK0,	setifflags),
	DEF_CMD("link1",	IFF_LINK1,	setifflags),
	DEF_CMD("-link1",	-IFF_LINK1,	setifflags),
	DEF_CMD("link2",	IFF_LINK2,	setifflags),
	DEF_CMD("-link2",	-IFF_LINK2,	setifflags),
	DEF_CMD("monitor",	IFF_MONITOR,	setifflags),
	DEF_CMD("-monitor",	-IFF_MONITOR,	setifflags),
	DEF_CMD("staticarp",	IFF_STATICARP,	setifflags),
	DEF_CMD("-staticarp",	-IFF_STATICARP,	setifflags),
	DEF_CMD("polling",	IFF_NPOLLING,	setifflags),
	DEF_CMD("-polling",	-IFF_NPOLLING,	setifflags),
	DEF_CMD("npolling",	IFF_NPOLLING,	setifflags),
	DEF_CMD("-npolling",	-IFF_NPOLLING,	setifflags),
	DEF_CMD("rxcsum",	IFCAP_RXCSUM,	setifcap),
	DEF_CMD("-rxcsum",	-IFCAP_RXCSUM,	setifcap),
	DEF_CMD("txcsum",	IFCAP_TXCSUM,	setifcap),
	DEF_CMD("-txcsum",	-IFCAP_TXCSUM,	setifcap),
	DEF_CMD("netcons",	IFCAP_NETCONS,	setifcap),
	DEF_CMD("-netcons",	-IFCAP_NETCONS,	setifcap),
	DEF_CMD("rss",		IFCAP_RSS,	setifcap),
	DEF_CMD("-rss",		-IFCAP_RSS,	setifcap),
	DEF_CMD("tso",		IFCAP_TSO,	setifcap),
	DEF_CMD("-tso",		-IFCAP_TSO,	setifcap),
	DEF_CMD("normal",	-IFF_LINK0,	setifflags),
	DEF_CMD("compress",	IFF_LINK0,	setifflags),
	DEF_CMD("noicmp",	IFF_LINK1,	setifflags),
	DEF_CMD_ARG("mtu",			setifmtu),
	DEF_CMD_ARG("name",			setifname),
	DEF_CMD_ARG("pollcpu",			setifpollcpu),
	DEF_CMD_ARG("tsolen",			setiftsolen)
};

static __constructor(101) void
ifconfig_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(basic_cmds);  i++)
		cmd_register(&basic_cmds[i]);
}
