/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Max Laier. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sbin/ifconfig/ifgroup.c 326276 2017-11-27 15:37:16Z pfg $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ifconfig.h"

static void
setifgroup(const char *group_name, int d __unused, int s,
	   const struct afswtch *rafp __unused)
{
	struct ifgroupreq ifgr;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, name, IFNAMSIZ);

	if (group_name[0] && isdigit(group_name[strlen(group_name) - 1]))
		errx(1, "setifgroup: group names may not end in a digit");

	if (strlcpy(ifgr.ifgr_group, group_name, IFNAMSIZ) >= IFNAMSIZ)
		errx(1, "setifgroup: group name too long");
	if (ioctl(s, SIOCAIFGROUP, (caddr_t)&ifgr) == -1 && errno != EEXIST)
		err(1," SIOCAIFGROUP");
}

static void
unsetifgroup(const char *group_name, int d __unused, int s,
	     const struct afswtch *rafp __unused)
{
	struct ifgroupreq ifgr;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, name, IFNAMSIZ);

	if (group_name[0] && isdigit(group_name[strlen(group_name) - 1]))
		errx(1, "unsetifgroup: group names may not end in a digit");

	if (strlcpy(ifgr.ifgr_group, group_name, IFNAMSIZ) >= IFNAMSIZ)
		errx(1, "unsetifgroup: group name too long");
	if (ioctl(s, SIOCDIFGROUP, (caddr_t)&ifgr) == -1 && errno != ENOENT)
		err(1, "SIOCDIFGROUP");
}

static void
getifgroups(int s)
{
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	size_t			 len, cnt;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, name, IFNAMSIZ);

	if (ioctl(s, SIOCGIFGROUP, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY)
			return;
		else
			err(1, "SIOCGIFGROUP");
	}

	len = ifgr.ifgr_len;
	ifgr.ifgr_groups = calloc(len / sizeof(struct ifg_req),
				  sizeof(struct ifg_req));
	if (ifgr.ifgr_groups == NULL)
		err(1, "getifgroups");
	if (ioctl(s, SIOCGIFGROUP, (caddr_t)&ifgr) == -1)
		err(1, "SIOCGIFGROUP");

	cnt = 0;
	ifg = ifgr.ifgr_groups;
	for (; ifg && len >= sizeof(struct ifg_req); ifg++) {
		len -= sizeof(struct ifg_req);
		if (strcmp(ifg->ifgrq_group, "all") != 0) {
			if (cnt == 0)
				printf("\tgroups:");
			cnt++;
			printf(" %s", ifg->ifgrq_group);
		}
	}
	if (cnt)
		printf("\n");

	free(ifgr.ifgr_groups);
}

static void
printgroup(const char *groupname)
{
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	size_t			 len, cnt = 0;
	int			 s;

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s == -1)
		err(1, "socket(AF_LOCAL,SOCK_DGRAM)");

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, groupname, sizeof(ifgr.ifgr_name));
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY ||
		    errno == ENOENT)
			exit(exit_code);
		else
			err(1, "SIOCGIFGMEMB");
	}

	len = ifgr.ifgr_len;
	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
		err(1, "printgroup");
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1)
		err(1, "SIOCGIFGMEMB");

	for (ifg = ifgr.ifgr_groups;
	     ifg && len >= sizeof(struct ifg_req);
	     ifg++) {
		len -= sizeof(struct ifg_req);
		printf("%s\n", ifg->ifgrq_member);
		cnt++;
	}
	free(ifgr.ifgr_groups);

	exit(exit_code);
}

static struct cmd group_cmds[] = {
	DEF_CMD_ARG("group",	setifgroup),
	DEF_CMD_ARG("-group",	unsetifgroup),
};
static struct afswtch af_group = {
	.af_name		= "af_group",
	.af_af			= AF_UNSPEC,
	.af_other_status	= getifgroups,
};
static struct option group_gopt = { "g:", "[-g groupname]", printgroup, NULL };

static __constructor(101) void
group_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(group_cmds);  i++)
		cmd_register(&group_cmds[i]);
	af_register(&af_group);
	opt_register(&group_gopt);
}
