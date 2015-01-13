/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
/*
 * Use: pipe syslog auth output to this program.
 *
 * Detects failed ssh login attempts and maps out the originating IP and
 * issues adds to a PF table <lockout> using 'pfctl -tlockout -Tadd' commands.
 *
 * /etc/syslog.conf line example:
 *	auth.info;authpriv.info		|exec /usr/sbin/sshlockout lockout
 *
 * Also suggest a cron entry to clean out the PF table at least once a day.
 *	3 3 * * *       pfctl -tlockout -Tflush
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>

typedef struct iphist {
	struct iphist *next;
	struct iphist *hnext;
	char	*ips;
	time_t	t;
	int	hv;
} iphist_t;

#define HSIZE		1024
#define HMASK		(HSIZE - 1)
#define MAXHIST		100
#define SSHLIMIT	5		/* per hour */

static iphist_t *hist_base;
static iphist_t **hist_tail = &hist_base;
static iphist_t *hist_hash[HSIZE];
static int hist_count = 0;

static char *pftable = NULL;

static void init_iphist(void);
static void checkline(char *buf);
static int insert_iph(const char *ips);
static void delete_iph(iphist_t *ip);

/*
 * Stupid simple string hash
 */
static __inline
int
iphash(const char *str)
{
	int hv = 0xA1B3569D;
	while (*str) {
		hv = (hv << 5) ^ *str ^ (hv >> 23);
		++str;
	}
	return hv;
}

int
main(int ac, char **av)
{
	char buf[1024];

	init_iphist();

	if (ac == 2 && av[1] != NULL) {
		pftable = av[1];
	}
	else {
		syslog(LOG_ERR, "sshlockout: invalid argument");
		return(1);
	}

	openlog("sshlockout", LOG_PID|LOG_CONS, LOG_AUTH);
	syslog(LOG_ERR, "sshlockout starting up");
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
		if (strstr(buf, "sshd") == NULL)
			continue;
		checkline(buf);
	}
	syslog(LOG_ERR, "sshlockout exiting");
	return(0);
}

static
void
checkline(char *buf)
{
	char ips[128];
	char *str;
	int n1;
	int n2;
	int n3;
	int n4;

	/*
	 * ssh login attempt with password (only hit if ssh allows
	 * password entry).  Root or admin.
	 */
	if ((str = strstr(buf, "Failed password for root from")) != NULL ||
	    (str = strstr(buf, "Failed password for admin from")) != NULL) {
		while (*str && (*str < '0' || *str > '9'))
			++str;
		if (sscanf(str, "%d.%d.%d.%d", &n1, &n2, &n3, &n4) == 4) {
			snprintf(ips, sizeof(ips), "%d.%d.%d.%d",
				 n1, n2, n3, n4);
			if (insert_iph(ips)) {
				syslog(LOG_ERR,
				       "Detected ssh password login attempt "
				       "for root or admin, locking out %s\n",
				       ips);
				snprintf(buf, sizeof(buf),
					 "pfctl -t%s -Tadd %s",
					 pftable, ips);
				system(buf);
			}
		}
		return;
	}

	/*
	 * ssh login attempt with password (only hit if ssh allows password
	 * entry).  Non-existant user.
	 */
	if ((str = strstr(buf, "Failed password for invalid user")) != NULL) {
		str += 32;
		while (*str == ' ')
			++str;
		while (*str && *str != ' ')
			++str;
		if (strncmp(str, " from", 5) == 0 &&
		    sscanf(str + 5, "%d.%d.%d.%d", &n1, &n2, &n3, &n4) == 4) {
			snprintf(ips, sizeof(ips), "%d.%d.%d.%d",
				 n1, n2, n3, n4);
			if (insert_iph(ips)) {
				syslog(LOG_ERR,
				       "Detected ssh password login attempt "
				       "for an invalid user, locking out %s\n",
				       ips);
				snprintf(buf, sizeof(buf),
					 "pfctl -t%s -Tadd %s",
					 pftable, ips);
				system(buf);
			}
		}
		return;
	}

	/*
	 * Premature disconnect in pre-authorization phase, typically an
	 * attack but require 5 attempts in an hour before cleaning it out.
	 */
	if ((str = strstr(buf, "Received disconnect from ")) != NULL &&
	    strstr(buf, "[preauth]") != NULL) {
		if (sscanf(str + 25, "%d.%d.%d.%d", &n1, &n2, &n3, &n4) == 4) {
			snprintf(ips, sizeof(ips), "%d.%d.%d.%d",
				 n1, n2, n3, n4);
			if (insert_iph(ips)) {
				syslog(LOG_ERR,
				       "Detected ssh password login attempt "
				       "for an invalid user, locking out %s\n",
				       ips);
				snprintf(buf, sizeof(buf),
					 "pfctl -t%s -Tadd %s",
					 pftable, ips);
				system(buf);
			}
		}
		return;
	}
}

/*
 * Insert IP record
 */
static
int
insert_iph(const char *ips)
{
	iphist_t *ip = malloc(sizeof(*ip));
	iphist_t *scan;
	time_t t = time(NULL);
	int found;

	ip->hv = iphash(ips);
	ip->ips = strdup(ips);
	ip->t = t;

	ip->hnext = hist_hash[ip->hv & HMASK];
	hist_hash[ip->hv & HMASK] = ip;
	ip->next = NULL;
	*hist_tail = ip;
	hist_tail = &ip->next;
	++hist_count;

	/*
	 * hysteresis
	 */
	if (hist_count > MAXHIST + 16) {
		while (hist_count > MAXHIST)
			delete_iph(hist_base);
	}

	/*
	 * Check limit
	 */
	found = 0;
	for (scan = hist_hash[ip->hv & HMASK]; scan; scan = scan->hnext) {
		if (scan->hv == ip->hv && strcmp(scan->ips, ip->ips) == 0) {
			int dt = (int)(t - ip->t);
			if (dt < 60 * 60) {
				++found;
				if (found > SSHLIMIT)
					break;
			}
		}
	}
	return (found > SSHLIMIT);
}

/*
 * Delete an ip record.  Note that we always delete from the head of the
 * list, but we will still wind up scanning hash chains.
 */
static
void
delete_iph(iphist_t *ip)
{
	iphist_t **scanp;
	iphist_t *scan;

	scanp = &hist_base;
	while ((scan = *scanp) != ip) {
		scanp = &scan->next;
	}
	*scanp = ip->next;
	if (hist_tail == &ip->next)
		hist_tail = scanp;

	scanp = &hist_hash[ip->hv & HMASK];
	while ((scan = *scanp) != ip) {
		scanp = &scan->hnext;
	}
	*scanp = ip->hnext;

	--hist_count;
	free(ip);
}

static
void
init_iphist(void) {
	hist_base = NULL;
	hist_tail = &hist_base;
	for (int i = 0; i < HSIZE; i++) {
		hist_hash[i] = NULL;
	}
	hist_count = 0;
}
