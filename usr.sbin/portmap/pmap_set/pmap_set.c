 /*
  * pmap_set - set portmapper table from data produced by pmap_dump
  *
  * Author: Wietse Venema (wietse@wzv.win.tue.nl), dept. of Mathematics and
  * Computing Science, Eindhoven University of Technology, The Netherlands.
  */

/*
 * @(#) pmap_set.c 1.1 92/06/11 22:53:16
 * $FreeBSD: src/usr.sbin/portmap/pmap_set/pmap_set.c,v 1.6 2000/01/15 23:08:30 brian Exp $
 * $DragonFly: src/usr.sbin/portmap/pmap_set/pmap_set.c,v 1.3 2003/11/03 19:31:40 eirikn Exp $
 */
#include <err.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef SYSV40
#include <netinet/in.h>
#endif
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

static int parse_line(char *, u_long *, u_long *, int *, unsigned *);

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct sockaddr_in addr;
    char    buf[BUFSIZ];
    u_long  prog;
    u_long  vers;
    int     prot;
    unsigned port;

    get_myaddress(&addr);

    while (fgets(buf, sizeof(buf), stdin)) {
	if (parse_line(buf, &prog, &vers, &prot, &port) == 0) {
	    warnx("malformed line: %s", buf);
	    return (1);
	}
	if (pmap_set(prog, vers, prot, (unsigned short) port) == 0)
	    warnx("not registered: %s", buf);
    }
    return (0);
}

/* parse_line - convert line to numbers */

static int
parse_line(buf, prog, vers, prot, port)
    char *buf;
    u_long *prog, *vers;
    int *prot;
    unsigned *port;
{
    char    proto_name[BUFSIZ];

    if (sscanf(buf, "%lu %lu %s %u", prog, vers, proto_name, port) != 4) {
	return (0);
    }
    if (strcmp(proto_name, "tcp") == 0) {
	*prot = IPPROTO_TCP;
	return (1);
    }
    if (strcmp(proto_name, "udp") == 0) {
	*prot = IPPROTO_UDP;
	return (1);
    }
    if (sscanf(proto_name, "%d", prot) == 1) {
	return (1);
    }
    return (0);
}
