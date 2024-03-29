/*-
 * $FreeBSD: head/sbin/ifconfig/iflagg.c 249897 2013-04-25 16:34:04Z glebius $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/lagg/if_lagg.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static char lacpbuf[120];	/* LACP peer '[(a,a,a),(p,p,p)]' */

static void
setlaggport(const char *val, int d __unused, int s,
	    const struct afswtch *afp __unused)
{
	struct lagg_reqport rp;

	memset(&rp, 0, sizeof(rp));
	strlcpy(rp.rp_ifname, IfName, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	/*
	 * Do not exit with an error here.  Doing so permits a failed NIC
	 * to take down an entire lagg.
	 *
	 * Don't error at all if the port is already in the lagg.
	 */
	if (ioctl(s, SIOCSLAGGPORT, &rp) && errno != EEXIST) {
		warnx("%s %s: SIOCSLAGGPORT: %s",
		      IfName, val, strerror(errno));
		exit_code = 1;
	}
}

static void
unsetlaggport(const char *val, int d __unused, int s,
	      const struct afswtch *afp __unused)
{
	struct lagg_reqport rp;

	memset(&rp, 0, sizeof(rp));
	strlcpy(rp.rp_ifname, IfName, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	if (ioctl(s, SIOCSLAGGDELPORT, &rp))
		err(1, "SIOCSLAGGDELPORT");
}

static void
setlaggproto(const char *val, int d __unused, int s,
	     const struct afswtch *afp __unused)
{
	struct lagg_protos lpr[] = LAGG_PROTOS;
	struct lagg_reqall ra;
	size_t i;

	memset(&ra, 0, sizeof(ra));
	ra.ra_proto = LAGG_PROTO_MAX;

	for (i = 0; i < nitems(lpr); i++) {
		if (strcmp(val, lpr[i].lpr_name) == 0) {
			ra.ra_proto = lpr[i].lpr_proto;
			break;
		}
	}
	if (ra.ra_proto == LAGG_PROTO_MAX)
		errx(1, "Invalid aggregation protocol: %s", val);

	strlcpy(ra.ra_ifname, IfName, sizeof(ra.ra_ifname));
	if (ioctl(s, SIOCSLAGG, &ra) != 0)
		err(1, "SIOCSLAGG");
}

static void
setlagghash(const char *val, int d __unused, int s,
	    const struct afswtch *afp __unused)
{
	struct lagg_reqflags rf;
	char *str, *tmp, *tok;

	rf.rf_flags = 0;
	str = tmp = strdup(val);
	while ((tok = strsep(&tmp, ",")) != NULL) {
		if (strcmp(tok, "l2") == 0)
			rf.rf_flags |= LAGG_F_HASHL2;
		else if (strcmp(tok, "l3") == 0)
			rf.rf_flags |= LAGG_F_HASHL3;
		else if (strcmp(tok, "l4") == 0)
			rf.rf_flags |= LAGG_F_HASHL4;
		else
			errx(1, "Invalid lagghash option: %s", tok);
	}
	free(str);
	if (rf.rf_flags == 0)
		errx(1, "No lagghash options supplied");

	strlcpy(rf.rf_ifname, IfName, sizeof(rf.rf_ifname));
	if (ioctl(s, SIOCSLAGGHASH, &rf))
		err(1, "SIOCSLAGGHASH");
}

static char *
lacp_format_mac(const uint8_t *mac, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%02X-%02X-%02X-%02X-%02X-%02X",
	    (int)mac[0], (int)mac[1], (int)mac[2], (int)mac[3],
	    (int)mac[4], (int)mac[5]);

	return (buf);
}

static char *
lacp_format_peer(struct lacp_opreq *req, const char *sep)
{
	char macbuf1[20];
	char macbuf2[20];

	snprintf(lacpbuf, sizeof(lacpbuf),
	    "[(%04X,%s,%04X,%04X,%04X),%s(%04X,%s,%04X,%04X,%04X)]",
	    req->actor_prio,
	    lacp_format_mac(req->actor_mac, macbuf1, sizeof(macbuf1)),
	    req->actor_key, req->actor_portprio, req->actor_portno, sep,
	    req->partner_prio,
	    lacp_format_mac(req->partner_mac, macbuf2, sizeof(macbuf2)),
	    req->partner_key, req->partner_portprio, req->partner_portno);

	return(lacpbuf);
}

static void
lagg_status(int s)
{
	struct lagg_protos lpr[] = LAGG_PROTOS;
	struct lagg_reqport rp, rpbuf[LAGG_MAX_PORTS];
	struct lagg_reqall ra;
	struct lagg_reqflags rf;
	struct lacp_opreq *lp;
	const char *proto = "<unknown>";
	bool isport = false;
	size_t i;

	memset(&rp, 0, sizeof(rp));
	memset(&ra, 0, sizeof(ra));

	strlcpy(rp.rp_ifname, IfName, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, IfName, sizeof(rp.rp_portname));

	if (ioctl(s, SIOCGLAGGPORT, &rp) == 0)
		isport = true;

	strlcpy(ra.ra_ifname, IfName, sizeof(ra.ra_ifname));
	ra.ra_size = sizeof(rpbuf);
	ra.ra_port = rpbuf;

	strlcpy(rf.rf_ifname, IfName, sizeof(rf.rf_ifname));
	if (ioctl(s, SIOCGLAGGFLAGS, &rf) != 0)
		rf.rf_flags = 0;

	if (ioctl(s, SIOCGLAGG, &ra) == 0) {
		lp = (struct lacp_opreq *)&ra.ra_lacpreq;

		for (i = 0; i < nitems(lpr); i++) {
			if ((int)ra.ra_proto == lpr[i].lpr_proto) {
				proto = lpr[i].lpr_name;
				break;
			}
		}

		printf("\tlaggproto %s", proto);
		if (rf.rf_flags & LAGG_F_HASHMASK) {
			const char *sep = "";

			printf(" lagghash ");
			if (rf.rf_flags & LAGG_F_HASHL2) {
				printf("%sl2", sep);
				sep = ",";
			}
			if (rf.rf_flags & LAGG_F_HASHL3) {
				printf("%sl3", sep);
				sep = ",";
			}
			if (rf.rf_flags & LAGG_F_HASHL4) {
				printf("%sl4", sep);
				sep = ",";
			}
		}
		if (isport)
			printf(" laggdev %s", rp.rp_ifname);
		putchar('\n');
		if (verbose && ra.ra_proto == LAGG_PROTO_LACP)
			printf("\tlag id: %s\n",
			    lacp_format_peer(lp, "\n\t\t "));

		for (i = 0; i < (size_t)ra.ra_ports; i++) {
			lp = (struct lacp_opreq *)&rpbuf[i].rp_lacpreq;
			printf("\tlaggport: %s ", rpbuf[i].rp_portname);
			printb("flags", rpbuf[i].rp_flags, LAGG_PORT_BITS);
			if (verbose && ra.ra_proto == LAGG_PROTO_LACP)
				printf(" state=%X", lp->actor_state);
			putchar('\n');
			if (verbose && ra.ra_proto == LAGG_PROTO_LACP)
				printf("\t\t%s\n",
				    lacp_format_peer(lp, "\n\t\t "));
		}

		if (0 /* XXX */) {
			printf("\tsupported aggregation protocols:\n");
			for (i = 0; i < nitems(lpr); i++)
				printf("\t\tlaggproto %s\n", lpr[i].lpr_name);
		}
	}
}

static struct cmd lagg_cmds[] = {
	DEF_CMD_ARG("laggport",		setlaggport),
	DEF_CMD_ARG("-laggport",	unsetlaggport),
	DEF_CMD_ARG("laggproto",	setlaggproto),
	DEF_CMD_ARG("lagghash",		setlagghash),
};
static struct afswtch af_lagg = {
	.af_name	= "af_lagg",
	.af_af		= AF_UNSPEC,
	.af_other_status = lagg_status,
};

__constructor(142)
static void
lagg_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(lagg_cmds);  i++)
		cmd_register(&lagg_cmds[i]);

	af_register(&af_lagg);
}
