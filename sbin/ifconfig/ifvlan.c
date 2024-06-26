/*
 * Copyright (c) 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/ifconfig/ifvlan.c,v 1.7.2.4 2006/02/09 10:48:43 yar Exp $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/vlan/if_vlan_var.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static struct vlanreq		__vreq;
static int			__have_dev = 0;
static int			__have_tag = 0;

static void
vlan_status(int s)
{
	struct vlanreq		vreq;
	struct ifreq		ifr;

	memset(&ifr, 0, sizeof(ifr));
	memset(&vreq, 0, sizeof(vreq));

	strlcpy(ifr.ifr_name, IfName, sizeof(ifr.ifr_name));
	ifr.ifr_data = &vreq;

	if (ioctl(s, SIOCGETVLAN, &ifr) == -1)
		return;

	printf("\tvlan: %d parent interface: %s\n", vreq.vlr_tag,
	       vreq.vlr_parent[0] == '\0' ? "<none>" : vreq.vlr_parent);
}

static void
setvlantag(const char *val, int d __unused, int s __unused,
	   const struct afswtch *afp __unused)
{
	char			*endp;
	u_long			ul;

	ul = strtoul(val, &endp, 0);
	if (*endp != '\0')
		errx(1, "invalid value for vlan");
	__vreq.vlr_tag = ul;
	/* check if the value can be represented in vlr_tag */
	if (__vreq.vlr_tag != ul)
		errx(1, "value for vlan out of range");
	/* the kernel will do more specific checks on vlr_tag */
	__have_tag = 1;
}

static void
setvlandev(const char *val, int d __unused, int s __unused,
	   const struct afswtch *afp __unused)
{
	strlcpy(__vreq.vlr_parent, val, sizeof(__vreq.vlr_parent));
	__have_dev = 1;
}

static void
unsetvlandev(const char *val, int d __unused, int s,
	     const struct afswtch *afp __unused)
{
	struct ifreq		ifr;

	if (val != NULL)
		warnx("argument to -vlandev is useless and hence deprecated");

	memset(&ifr, 0, sizeof(ifr));
	memset(&__vreq, 0, sizeof(__vreq));

	strlcpy(ifr.ifr_name, IfName, sizeof(ifr.ifr_name));
	ifr.ifr_data = &__vreq;

#if 0	/* this code will be of use when we can alter vlan or vlandev only */
	if (ioctl(s, SIOCGETVLAN, &ifr) == -1)
		err(1, "SIOCGETVLAN");

	memset(&__vreq.vlr_parent, 0, sizeof(__vreq.vlr_parent));
	__vreq.vlr_tag = 0; /* XXX clear parent only (no kernel support now) */
#endif

	if (ioctl(s, SIOCSETVLAN, &ifr) == -1)
		err(1, "SIOCSETVLAN");
	__have_dev = __have_tag = 0;
}

static void
vlan_cb(int s, void *arg __unused)
{
	struct ifreq		ifr;

	if (__have_tag ^ __have_dev)
		errx(1, "both vlan and vlandev must be specified");

	if (__have_tag && __have_dev) {
		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, IfName, sizeof(ifr.ifr_name));
		ifr.ifr_data = &__vreq;
		if (ioctl(s, SIOCSETVLAN, &ifr) == -1)
			err(1, "SIOCSETVLAN");
	}
}

static struct cmd vlan_cmds[] = {
	DEF_CMD_ARG("vlan",				setvlantag),
	DEF_CMD_ARG("vlandev",				setvlandev),
	/* XXX For compatibility.  Should become DEF_CMD() some day. */
	DEF_CMD_OPTARG("-vlandev",			unsetvlandev),
	DEF_CMD("vlanmtu",	IFCAP_VLAN_MTU,		setifcap),
	DEF_CMD("-vlanmtu",	-IFCAP_VLAN_MTU,	setifcap),
	DEF_CMD("vlanhwtag",	IFCAP_VLAN_HWTAGGING,	setifcap),
	DEF_CMD("-vlanhwtag",	-IFCAP_VLAN_HWTAGGING,	setifcap),
};
static struct afswtch af_vlan = {
	.af_name	= "af_vlan",
	.af_af		= AF_UNSPEC,
	.af_other_status = vlan_status,
};

__constructor(124)
static void
vlan_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(vlan_cmds);  i++)
		cmd_register(&vlan_cmds[i]);

	af_register(&af_vlan);
	callback_register(vlan_cb, NULL);
}
