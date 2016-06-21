/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
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
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <sysexits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/ethernet.h>

#include "../../sys/net/ipfw3/ip_fw3.h"
#include "../../sys/net/ipfw3/ip_fw3_table.h"
#include "../../sys/net/ipfw3/ip_fw3_sync.h"
#include "../../sys/net/dummynet3/ip_dummynet3.h"
#include "../../sys/net/libalias/alias.h"
#include "../../sys/net/ipfw3_basic/ip_fw3_basic.h"
#include "../../sys/net/ipfw3_nat/ip_fw3_nat.h"

#include "ipfw3.h"
#include "ipfw3nat.h"

extern int verbose;
extern int do_time;
extern int do_quiet;
extern int do_force;

struct char_int_map nat_params[] = {
	{ "ip", 		TOK_IP },
	{ "if", 		TOK_IF },
	{ "log", 		TOK_ALOG },
	{ "deny_in", 		TOK_DENY_INC },
	{ "same_ports", 	TOK_SAME_PORTS },
	{ "unreg_only", 	TOK_UNREG_ONLY },
	{ "reset", 		TOK_RESET_ADDR },
	{ "reverse", 		TOK_ALIAS_REV },
	{ "proxy_only", 	TOK_PROXY_ONLY },
	{ "redirect_addr", 	TOK_REDIR_ADDR },
	{ "redirect_port", 	TOK_REDIR_PORT },
	{ "redirect_proto", 	TOK_REDIR_PROTO },
	{ NULL, 0 },
};


void
nat_config(int ac, char **av)
{
	struct cfg_nat *n;	/* Nat instance configuration. */
	int i, len, off, tok;
	char *id, buf[NAT_BUF_LEN]; 	/* Buffer for serialized data. */

	len = NAT_BUF_LEN;
	/* Offset in buf: save space for n at the beginning. */
	off = sizeof(struct cfg_nat);
	memset(buf, 0, sizeof(buf));
	n = (struct cfg_nat *)buf;

	NEXT_ARG;
	/* Nat id. */
	if (ac && isdigit(**av)) {
		id = *av;
		i = atoi(*av);
		NEXT_ARG;
		n->id = i;
	} else
		errx(EX_DATAERR, "missing nat id");
	if (ac == 0)
		errx(EX_DATAERR, "missing option");

	while (ac > 0) {
		tok = match_token(nat_params, *av);
		NEXT_ARG;
		switch (tok) {
		case TOK_IP:
			if (ac == 0)
				errx(EX_DATAERR, "missing option");
			if (!inet_aton(av[0], &(n->ip)))
				errx(EX_DATAERR, "bad ip addr `%s'", av[0]);
			NEXT_ARG;
			break;
		case TOK_IF:
			if (ac == 0)
				errx(EX_DATAERR, "missing option");
			set_addr_dynamic(av[0], n);
			NEXT_ARG;
			break;
		case TOK_ALOG:
			n->mode |= PKT_ALIAS_LOG;
			break;
		case TOK_DENY_INC:
			n->mode |= PKT_ALIAS_DENY_INCOMING;
			break;
		case TOK_SAME_PORTS:
			n->mode |= PKT_ALIAS_SAME_PORTS;
			break;
		case TOK_UNREG_ONLY:
			n->mode |= PKT_ALIAS_UNREGISTERED_ONLY;
			break;
		case TOK_RESET_ADDR:
			n->mode |= PKT_ALIAS_RESET_ON_ADDR_CHANGE;
			break;
		case TOK_ALIAS_REV:
			n->mode |= PKT_ALIAS_REVERSE;
			break;
		case TOK_PROXY_ONLY:
			n->mode |= PKT_ALIAS_PROXY_ONLY;
			break;
		/*
		 * All the setup_redir_* functions work
		 * directly in the final
		 * buffer, see above for details.
		 */
		case TOK_REDIR_ADDR:
		case TOK_REDIR_PORT:
		case TOK_REDIR_PROTO:
			switch (tok) {
			case TOK_REDIR_ADDR:
				i = setup_redir_addr(&buf[off], len, &ac, &av);
				break;
			case TOK_REDIR_PORT:
				i = setup_redir_port(&buf[off], len, &ac, &av);
				break;
			case TOK_REDIR_PROTO:
				i = setup_redir_proto(&buf[off], len, &ac, &av);
				break;
			}
			n->redir_cnt++;
			off += i;
			len -= i;
			break;
		default:
			errx(EX_DATAERR, "unrecognised option ``%s''", av[-1]);
		}
	}
	i = do_set_x(IP_FW_NAT_ADD, buf, off);
	if (i) {
		err(1, "do_set_x(%s)", "IP_FW_NAT_ADD");
	}

	/* After every modification, we show the resultant rule. */
	int _ac = 2;
	char *_av[] = {"config", id};
	nat_show(_ac, _av);
}

void
nat_show_config(char *buf)
{
	struct cfg_nat *n;
	struct cfg_redir *t;
	struct cfg_spool *s;
	struct protoent *p;
	int i, cnt, flag, off;

	n = (struct cfg_nat *)buf;
	flag = 1;
	off = sizeof(*n);
	printf("ipfw3 nat %u config", n->id);
	if (strlen(n->if_name) != 0)
		printf(" if %s", n->if_name);
	else if (n->ip.s_addr != 0)
		printf(" ip %s", inet_ntoa(n->ip));
	while (n->mode != 0) {
		if (n->mode & PKT_ALIAS_LOG) {
			printf(" log");
			n->mode &= ~PKT_ALIAS_LOG;
		} else if (n->mode & PKT_ALIAS_DENY_INCOMING) {
			printf(" deny_in");
			n->mode &= ~PKT_ALIAS_DENY_INCOMING;
		} else if (n->mode & PKT_ALIAS_SAME_PORTS) {
			printf(" same_ports");
			n->mode &= ~PKT_ALIAS_SAME_PORTS;
		} else if (n->mode & PKT_ALIAS_UNREGISTERED_ONLY) {
			printf(" unreg_only");
			n->mode &= ~PKT_ALIAS_UNREGISTERED_ONLY;
		} else if (n->mode & PKT_ALIAS_RESET_ON_ADDR_CHANGE) {
			printf(" reset");
			n->mode &= ~PKT_ALIAS_RESET_ON_ADDR_CHANGE;
		} else if (n->mode & PKT_ALIAS_REVERSE) {
			printf(" reverse");
			n->mode &= ~PKT_ALIAS_REVERSE;
		} else if (n->mode & PKT_ALIAS_PROXY_ONLY) {
			printf(" proxy_only");
			n->mode &= ~PKT_ALIAS_PROXY_ONLY;
		}
	}

	/* Print all the redirect's data configuration. */
	for (cnt = 0; cnt < n->redir_cnt; cnt++) {
		t = (struct cfg_redir *)&buf[off];
		off += SOF_REDIR;
		switch (t->mode) {
		case REDIR_ADDR:
			printf(" redirect_addr");
			if (t->spool_cnt == 0) {
				printf(" %s", inet_ntoa(t->laddr));
			} else {
				for (i = 0; i < t->spool_cnt; i++) {
					s = (struct cfg_spool *)&buf[off];
					if (i)
						printf(", ");
					else
						printf(" ");
					printf("%s", inet_ntoa(s->addr));
					off += SOF_SPOOL;
				}
			}
			printf(" %s", inet_ntoa(t->paddr));
			break;
		case REDIR_PORT:
			p = getprotobynumber(t->proto);
			printf(" redirect_port %s ", p->p_name);
			if (!t->spool_cnt) {
				printf("%s:%u", inet_ntoa(t->laddr), t->lport);
				if (t->pport_cnt > 1) {
					printf("-%u", t->lport +
							t->pport_cnt - 1);
				}
			} else
				for (i=0; i < t->spool_cnt; i++) {
					s = (struct cfg_spool *)&buf[off];
					if (i) {
						printf(", ");
					}
					printf("%s:%u", inet_ntoa(s->addr),
							s->port);
					off += SOF_SPOOL;
				}

			printf(" ");
			if (t->paddr.s_addr) {
				printf("%s:", inet_ntoa(t->paddr));
			}
			printf("%u", t->pport);
			if (!t->spool_cnt && t->pport_cnt > 1) {
				printf("-%u", t->pport + t->pport_cnt - 1);
			}

			if (t->raddr.s_addr) {
				printf(" %s", inet_ntoa(t->raddr));
				if (t->rport) {
					printf(":%u", t->rport);
					if (!t->spool_cnt && t->rport_cnt > 1) {
						printf("-%u", t->rport +
							t->rport_cnt - 1);
					}
				}
			}
			break;
		case REDIR_PROTO:
			p = getprotobynumber(t->proto);
			printf(" redirect_proto %s %s", p->p_name,
					inet_ntoa(t->laddr));
			if (t->paddr.s_addr != 0) {
				printf(" %s", inet_ntoa(t->paddr));
				if (t->raddr.s_addr) {
					printf(" %s", inet_ntoa(t->raddr));
				}
			}
			break;
		default:
			errx(EX_DATAERR, "unknown redir mode");
			break;
		}
	}
	printf("\n");
}


void
nat_show(int ac, char **av)
{
	struct cfg_nat *n;
	struct cfg_redir *e;
	int i, nbytes, nalloc, size;
	int nat_cnt, redir_cnt, nat_id;
	uint8_t *data;

	nalloc = 1024;
	size = 0;
	data = NULL;

	NEXT_ARG;

	if (ac == 0)
		nat_id = 0;
	else
		nat_id = strtoul(*av, NULL, 10);

	nbytes = nalloc;
	while (nbytes >= nalloc) {
		nalloc = nalloc * 2;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL) {
			err(EX_OSERR, "realloc");
		}
		if (do_get_x(IP_FW_NAT_GET, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_FW_NAT_GET)");
		}
	}

	if (nbytes == 0) {
		exit(EX_OK);
	}

	nat_cnt = *((int *)data);
	for (i = sizeof(nat_cnt); nat_cnt; nat_cnt--) {
		n = (struct cfg_nat *)&data[i];
		if (n->id >= 0 && n->id <= IPFW_DEFAULT_RULE) {
			if (nat_id == 0 || n->id == nat_id)
				nat_show_config(&data[i]);
		}
		i += sizeof(struct cfg_nat);
		for (redir_cnt = 0; redir_cnt < n->redir_cnt; redir_cnt++) {
			e = (struct cfg_redir *)&data[i];
			i += sizeof(struct cfg_redir) +
				e->spool_cnt * sizeof(struct cfg_spool);
		}
	}
}

int
setup_redir_port(char *spool_buf, int len, int *_ac, char ***_av)
{
	char **av, *sep, *protoName;
	char tmp_spool_buf[NAT_BUF_LEN];
	int ac, space, lsnat;
	struct cfg_redir *r;
	struct cfg_spool *tmp;
	u_short numLocalPorts;
	port_range portRange;

	av = *_av;
	ac = *_ac;
	space = 0;
	lsnat = 0;
	numLocalPorts = 0;

	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}

	r->mode = REDIR_PORT;
	/*
	 * Extract protocol.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing protocol");

	r->proto = str2proto(*av);
	protoName = *av;
	INC_ARGCV();

	/*
	 * Extract local address.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing local address");

	sep = strchr(*av, ',');
	/* LSNAT redirection syntax. */
	if (sep) {
		r->laddr.s_addr = INADDR_NONE;
		r->lport = ~0;
		numLocalPorts = 1;
		/* Preserve av, copy spool servers to tmp_spool_buf. */
		strncpy(tmp_spool_buf, *av, strlen(*av)+1);
		lsnat = 1;
	} else {
		if (str2addr_portrange (*av, &r->laddr,
					protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid local port range");

		r->lport = GETLOPORT(portRange);
		numLocalPorts = GETNUMPORTS(portRange);
	}
	INC_ARGCV();

	/*
	 * Extract public port and optionally address.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing public port");

	sep = strchr (*av, ':');
	if (sep) {
		if (str2addr_portrange (*av, &r->paddr, protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid public port range");
	} else {
		r->paddr.s_addr = INADDR_ANY;
		if (str2portrange(*av, protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid public port range");
	}

	r->pport = GETLOPORT(portRange);
	r->pport_cnt = GETNUMPORTS(portRange);
	INC_ARGCV();

	/*
	 * Extract remote address and optionally port.
	 */
	/*
	 * NB: isalpha(**av) => we've to check that next parameter is really an
	 * option for this redirect entry, else stop here processing arg[cv].
	 */
	if (ac != 0 && !isalpha(**av)) {
		sep = strchr (*av, ':');
		if (sep) {
			if (str2addr_portrange (*av, &r->raddr,
						protoName, &portRange) != 0) {
				errx(EX_DATAERR, "redirect_port:"
					"invalid remote port range");
			}
		} else {
			SETLOPORT(portRange, 0);
			SETNUMPORTS(portRange, 1);
			str2addr (*av, &r->raddr);
		}
		INC_ARGCV();
	} else {
		SETLOPORT(portRange, 0);
		SETNUMPORTS(portRange, 1);
		r->raddr.s_addr = INADDR_ANY;
	}
	r->rport = GETLOPORT(portRange);
	r->rport_cnt = GETNUMPORTS(portRange);

	/*
	 * Make sure port ranges match up, then add the redirect ports.
	 */
	if (numLocalPorts != r->pport_cnt) {
		errx(EX_DATAERR, "redirect_port:"
			"port ranges must be equal in size");
	}

	/* Remote port range is allowed to be '0' which means all ports. */
	if (r->rport_cnt != numLocalPorts &&
			(r->rport_cnt != 1 || r->rport != 0)) {
		errx(EX_DATAERR, "redirect_port: remote port must"
				"be 0 or equal to local port range in size");
	}

	/*
	 * Setup LSNAT server pool.
	 */
	if (lsnat) {
		sep = strtok(tmp_spool_buf, ", ");
		while (sep != NULL) {
			tmp = (struct cfg_spool *)spool_buf;
			if (len < SOF_SPOOL)
				goto nospace;

			len -= SOF_SPOOL;
			space += SOF_SPOOL;
			if (str2addr_portrange(sep,
				&tmp->addr, protoName, &portRange) != 0)
				errx(EX_DATAERR, "redirect_port:"
					"invalid local port range");
			if (GETNUMPORTS(portRange) != 1)
				errx(EX_DATAERR, "redirect_port: local port"
					"must be single in this context");
			tmp->port = GETLOPORT(portRange);
			r->spool_cnt++;
			/* Point to the next possible cfg_spool. */
			spool_buf = &spool_buf[SOF_SPOOL];
			sep = strtok(NULL, ", ");
		}
	}
	return (space);

nospace:
	errx(EX_DATAERR, "redirect_port: buf is too small\n");
}

int
setup_redir_proto(char *spool_buf, int len, int *_ac, char ***_av)
{
	struct protoent *protoent;
	struct cfg_redir *r;
	int ac, i, space;
	char **av;

	i=0;
	av = *_av;
	ac = *_ac;
	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}
	r->mode = REDIR_PROTO;
	/*
	 * Extract protocol.
	 */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_proto: missing protocol");

	protoent = getprotobyname(*av);
	if (protoent == NULL)
		errx(EX_DATAERR, "redirect_proto: unknown protocol %s", *av);
	else
		r->proto = protoent->p_proto;

	INC_ARGCV();

	/*
	 * Extract local address.
	 */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_proto: missing local address");
	else
		str2addr(*av, &r->laddr);
	INC_ARGCV();

	/*
	 * Extract optional public address.
	 */
	if (ac == 0) {
		r->paddr.s_addr = INADDR_ANY;
		r->raddr.s_addr = INADDR_ANY;
	} else {
		/* see above in setup_redir_port() */
		if (!isalpha(**av)) {
			str2addr(*av, &r->paddr);
			INC_ARGCV();

			/*
			 * Extract optional remote address.
			 */
			/* see above in setup_redir_port() */
			if (ac != 0 && !isalpha(**av)) {
				str2addr(*av, &r->raddr);
				INC_ARGCV();
			}
		}
	}
	return (space);

nospace:
	errx(EX_DATAERR, "redirect_proto: buf is too small\n");
}

int
str2proto(const char* str)
{
	if (!strcmp (str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp (str, "udp"))
		return IPPROTO_UDP;
	errx (EX_DATAERR, "unknown protocol %s. Expected tcp or udp", str);
}

int
str2addr_portrange (const char* str, struct in_addr* addr,
	char* proto, port_range *portRange)
{
	char*	ptr;

	ptr = strchr (str, ':');
	if (!ptr)
		errx (EX_DATAERR, "%s is missing port number", str);

	*ptr = '\0';
	++ptr;

	str2addr (str, addr);
	return str2portrange (ptr, proto, portRange);
}

/*
 * Search for interface with name "ifn", and fill n accordingly:
 *
 * n->ip		ip address of interface "ifn"
 * n->if_name copy of interface name "ifn"
 */
void
set_addr_dynamic(const char *ifn, struct cfg_nat *n)
{
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct sockaddr_dl *sdl;
	struct sockaddr_in *sin;
	char *buf, *lim, *next;
	size_t needed;
	int mib[6];
	int ifIndex, ifMTU;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	/*
	 * Get interface data.
	 */
	if (sysctl(mib, 6, NULL, &needed, NULL, 0) == -1)
		err(1, "iflist-sysctl-estimate");
	if ((buf = malloc(needed)) == NULL)
		errx(1, "malloc failed");
	if (sysctl(mib, 6, buf, &needed, NULL, 0) == -1)
		err(1, "iflist-sysctl-get");
	lim = buf + needed;
	/*
	 * Loop through interfaces until one with
	 * given name is found. This is done to
	 * find correct interface index for routing
	 * message processing.
	 */
	ifIndex	= 0;
	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		next += ifm->ifm_msglen;
		if (ifm->ifm_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
					"not understood", ifm->ifm_version);
			continue;
		}
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (strlen(ifn) == sdl->sdl_nlen &&
				strncmp(ifn, sdl->sdl_data,
					sdl->sdl_nlen) == 0) {
				ifIndex = ifm->ifm_index;
				ifMTU = ifm->ifm_data.ifi_mtu;
				break;
			}
		}
	}
	if (!ifIndex)
		errx(1, "unknown interface name %s", ifn);
	/*
	 * Get interface address.
	 */
	sin = NULL;
	while (next < lim) {
		ifam = (struct ifa_msghdr *)next;
		next += ifam->ifam_msglen;
		if (ifam->ifam_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
					"not understood", ifam->ifam_version);
			continue;
		}
		if (ifam->ifam_type != RTM_NEWADDR)
			break;
		if (ifam->ifam_addrs & RTA_IFA) {
			int i;
			char *cp = (char *)(ifam + 1);

			for (i = 1; i < RTA_IFA; i <<= 1) {
				if (ifam->ifam_addrs & i)
					cp += SA_SIZE((struct sockaddr *)cp);
			}
			if (((struct sockaddr *)cp)->sa_family == AF_INET) {
				sin = (struct sockaddr_in *)cp;
				break;
			}
		}
	}
	if (sin == NULL)
		errx(1, "%s: cannot get interface address", ifn);

	n->ip = sin->sin_addr;
	strncpy(n->if_name, ifn, IF_NAMESIZE);

	free(buf);
}

int
setup_redir_addr(char *spool_buf, int len, int *_ac, char ***_av)
{
	struct cfg_redir *r;
	struct cfg_spool *tmp;
	char **av, *sep;
	char tmp_spool_buf[NAT_BUF_LEN];
	int ac, i, space, lsnat;

	i=0;
	av = *_av;
	ac = *_ac;
	space = 0;
	lsnat = 0;
	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}

	r->mode = REDIR_ADDR;
	/* Extract local address. */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_addr: missing local address");

	sep = strchr(*av, ',');
	if (sep) {		/* LSNAT redirection syntax. */
		r->laddr.s_addr = INADDR_NONE;
		/* Preserve av, copy spool servers to tmp_spool_buf. */
		strncpy(tmp_spool_buf, *av, strlen(*av)+1);
		lsnat = 1;
	} else {
		str2addr(*av, &r->laddr);
	}
	INC_ARGCV();

	/* Extract public address. */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_addr: missing public address");

	str2addr(*av, &r->paddr);
	INC_ARGCV();

	/* Setup LSNAT server pool. */
	if (sep) {
		sep = strtok(tmp_spool_buf, ", ");
		while (sep != NULL) {
			tmp = (struct cfg_spool *)spool_buf;
			if (len < SOF_SPOOL)
				goto nospace;

			len -= SOF_SPOOL;
			space += SOF_SPOOL;
			str2addr(sep, &tmp->addr);
			tmp->port = ~0;
			r->spool_cnt++;
			/* Point to the next possible cfg_spool. */
			spool_buf = &spool_buf[SOF_SPOOL];
			sep = strtok(NULL, ", ");
		}
	}
	return(space);

nospace:
	errx(EX_DATAERR, "redirect_addr: buf is too small\n");
}

void
str2addr(const char* str, struct in_addr* addr)
{
	struct hostent* hp;

	if (inet_aton (str, addr))
		return;

	hp = gethostbyname (str);
	if (!hp)
		errx (1, "unknown host %s", str);

	memcpy (addr, hp->h_addr, sizeof (struct in_addr));
}

int
str2portrange(const char* str, const char* proto, port_range *portRange)
{
	struct servent*	sp;
	char*	sep;
	char*	end;
	u_short	loPort, hiPort;

	/* First see if this is a service, return corresponding port if so. */
	sp = getservbyname (str, proto);
	if (sp) {
		SETLOPORT(*portRange, ntohs(sp->s_port));
		SETNUMPORTS(*portRange, 1);
		return 0;
	}

	/* Not a service, see if it's a single port or port range. */
	sep = strchr (str, '-');
	if (sep == NULL) {
		SETLOPORT(*portRange, strtol(str, &end, 10));
		if (end != str) {
			/* Single port. */
			SETNUMPORTS(*portRange, 1);
			return 0;
		}

		/* Error in port range field. */
		errx (EX_DATAERR, "%s/%s: unknown service", str, proto);
	}

	/* Port range, get the values and sanity check. */
	sscanf (str, "%hu-%hu", &loPort, &hiPort);
	SETLOPORT(*portRange, loPort);
	SETNUMPORTS(*portRange, 0); 	/* Error by default */
	if (loPort <= hiPort)
		SETNUMPORTS(*portRange, hiPort - loPort + 1);

	if (GETNUMPORTS(*portRange) == 0)
		errx (EX_DATAERR, "invalid port range %s", str);

	return 0;
}

void
nat_delete_config(int ac, char *av[])
{
	NEXT_ARG;
	int i = 0;
	if (ac > 0) {
		i = atoi(*av);
	}
	if (do_set_x(IP_FW_NAT_DEL, &i, sizeof(i)) == -1)
		errx(EX_USAGE, "NAT %d in use or not exists", i);
}

void
nat_show_state(int ac, char **av)
{
	int nbytes, nalloc;
	int nat_id;
	uint8_t *data;

	nalloc = 1024;
	data = NULL;

	NEXT_ARG;
	if (ac == 0)
		nat_id = 0;
	else
		nat_id = strtoul(*av, NULL, 10);

	nbytes = nalloc;
	while (nbytes >= nalloc) {
		nalloc = nalloc * 2;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL) {
			err(EX_OSERR, "realloc");
		}
		memcpy(data, &nat_id, sizeof(int));
		if (do_get_x(IP_FW_NAT_GET_RECORD, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_FW_NAT_GET_RECORD)");
		}
	}
	if (nbytes == 0)
		exit(EX_OK);
	struct ipfw_ioc_nat_state *nat_state;
	nat_state =(struct ipfw_ioc_nat_state *)data;
	int count = nbytes / sizeof( struct ipfw_ioc_nat_state);
	int i, uptime_sec;
	uptime_sec = get_kern_boottime();
	for (i = 0; i < count; i ++) {
		printf("#%d ", nat_state->cpuid);
		printf("%s:%hu => ",inet_ntoa(nat_state->src_addr),
				htons(nat_state->src_port));
		printf("%s:%hu",inet_ntoa(nat_state->alias_addr),
				htons(nat_state->alias_port));
		printf(" -> %s:%hu ",inet_ntoa(nat_state->dst_addr),
				htons(nat_state->dst_port));
		if (do_time == 1) {
			char timestr[30];
			time_t t = _long_to_time(uptime_sec +
					nat_state->timestamp);
			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			printf("%s ", timestr);
		} else if (do_time == 2) {
			printf( "%10u ", uptime_sec + nat_state->timestamp);
		}
		struct protoent *pe = getprotobynumber(nat_state->link_type);
		printf("%s ", pe->p_name);
		printf(" %s", nat_state->is_outgoing? "out": "in");
		printf("\n");
		nat_state++;
	}
}

int
get_kern_boottime(void)
{
	struct timeval boottime;
	size_t size;
	int mib[2];
	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	size = sizeof(boottime);
	if (sysctl(mib, 2, &boottime, &size, NULL, 0) != -1 &&
			boottime.tv_sec != 0) {
		return boottime.tv_sec;
	}
	return -1;
}

void
nat_flush()
{
	int cmd = IP_FW_NAT_FLUSH;
	if (!do_force) {
		int c;

		printf("Are you sure? [yn] ");
		fflush(stdout);
		do {
			c = toupper(getc(stdin));
			while (c != '\n' && getc(stdin) != '\n')
				if (feof(stdin))
					return; /* and do not flush */
		} while (c != 'Y' && c != 'N');
		if (c == 'N')	/* user said no */
			return;
	}
	if (do_set_x(cmd, NULL, 0) < 0 ) {
		errx(EX_USAGE, "NAT configuration in use");
	}
	if (!do_quiet) {
		printf("Flushed all nat configurations");
	}
}

void
nat_main(int ac, char **av)
{
	if (!strncmp(*av, "config", strlen(*av))) {
		nat_config(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		nat_flush();
	} else if (!strncmp(*av, "show", strlen(*av)) ||
			!strncmp(*av, "list", strlen(*av))) {
		if (ac > 2 && isdigit(*(av[1]))) {
			char *p = av[1];
			av[1] = av[2];
			av[2] = p;
		}
		NEXT_ARG;
		if (!strncmp(*av, "config", strlen(*av))) {
			nat_show(ac, av);
		} else if (!strncmp(*av, "state", strlen(*av))) {
			nat_show_state(ac,av);
		} else {
			errx(EX_USAGE,
					"bad nat show command `%s'", *av);
		}
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		nat_delete_config(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw nat command `%s'", *av);
	}
}
