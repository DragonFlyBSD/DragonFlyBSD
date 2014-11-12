/*-
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
 * All rights reserved.
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
 * $FreeBSD: src/usr.sbin/jls/jls.c,v 1.3 2003/04/22 13:24:56 mike Exp $
 * $DragonFly: src/usr.sbin/jls/jls.c,v 1.2 2006/12/29 18:02:57 victor Exp $
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>

int
main(void)
{ 
	size_t len;
	char *jls; /* Jail list */
	char *curpos;
	char *nextpos;

	if (sysctlbyname("jail.list", NULL, &len, NULL, 0) == -1)
		err(1, "sysctlbyname(): jail.list");
retry:
	if (len == 0)
		exit(0);	

	jls = malloc(len);
	if (jls == NULL)
		err(1, "malloc failed");

	if (sysctlbyname("jail.list", jls, &len, NULL, 0) == -1) {
		if (errno == ENOMEM) {
			free(jls);
			goto retry;
		}
		err(1, "sysctlbyname(): jail.list");
	}
	printf("JID\tHostname\tPath\t\tIPs\n");
	curpos = jls;
	while (curpos) {
		char *str_jid;
		char *str_host;
		char *str_path;
		char *str_ips;

		nextpos = strchr(curpos, '\n');
		if (nextpos)
			*nextpos++ = 0;
		str_jid = strtok(curpos, " ");
		str_host = strtok(NULL, " ");
		str_path = strtok(NULL, " ");
		str_ips = strtok(NULL, "\n");

		printf("%-8s%-15s %-15s %s\n",
			str_jid,
			str_host,
			str_path,
			(str_ips ? str_ips : "(none)"));
		curpos = nextpos;
	}
	free(jls);
	exit(0);
}
