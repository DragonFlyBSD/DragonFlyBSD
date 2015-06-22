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
 * issues, in case of a PF firewall, adds to a PF table <lockout> using
 * 'pfctl -tlockout -Tadd' commands.
 *
 * /etc/syslog.conf line example:
 *	auth.info;authpriv.info		|exec /usr/sbin/sshlockout -pf lockout
 *
 * Also suggest a cron entry to clean out the PF table at least once a day.
 *	3 3 * * *       pfctl -tlockout -Tflush
 *
 * Alternatively there is an ipfw(8) mode (-ipfw <rulenum>).
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <ctype.h>
#include <stdbool.h>

typedef struct iphist {
	struct iphist *next;
	struct iphist *hnext;
	char	*ips;
	time_t	t;
	int	hv;
} iphist_t;

struct args {
	int   fw_type;
	char *arg1;
	char *arg2;
};

#define FW_IS_PF	1
#define FW_IS_IPFW	2

#define HSIZE		1024
#define HMASK		(HSIZE - 1)
#define MAXHIST		100
#define SSHLIMIT	5		/* per hour */
#define MAX_TABLE_NAME	20		/* PF table name limit */

static iphist_t *hist_base;
static iphist_t **hist_tail = &hist_base;
static iphist_t *hist_hash[HSIZE];
static int hist_count = 0;

static struct args args;

static void init_iphist(void);
static void checkline(char *buf);
static int insert_iph(const char *ips, time_t t);
static void delete_iph(iphist_t *ip);

static
void
block_ip(const char *ips) {
	char buf[128];
	int r = 0;

	switch (args.fw_type) {
		case FW_IS_PF:
			r = snprintf(buf, sizeof(buf),
				"pfctl -t%s -Tadd %s", args.arg1, ips);
			break;
		case FW_IS_IPFW:
			r = snprintf(buf, sizeof(buf),
				"ipfw add %s deny tcp from %s to me 22",
				args.arg1, ips);
			break;
	}

	if (r > 0 && (int)strlen(buf) == r) {
		system(buf);
	}
	else {
		syslog(LOG_ERR, "sshlockout: invalid command");
	}
}

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


static bool
parse_args(int ac, char **av)
{
	if (ac >= 2) {
		if (strcmp(av[1], "-pf") == 0) {
			// -pf <tablename>
			char *tablename = av[2];
			if (ac == 3 && tablename != NULL) {
				if (strlen(tablename) > 0 &&
				    strlen(tablename) < MAX_TABLE_NAME) {
					args.fw_type = FW_IS_PF;
					args.arg1 = tablename;
					args.arg2 = NULL;
					return true;
				}
			}
		}
		if (strcmp(av[1], "-ipfw") == 0) {
			// -ipfw <rule>
			char *rule = av[2];
			if (ac == 3 && rule != NULL) {
				if (strlen(rule) > 0 && strlen(rule) <= 5) {
					for (char *s = rule; *s; ++s) {
						if (!isdigit(*s))
							return false;
					}
					if (atoi(rule) < 1)
						return false;
					if (atoi(rule) > 65535)
						return false;
					args.fw_type = FW_IS_IPFW;
					args.arg1 = rule;
					args.arg2 = NULL;
					return true;
				}
			}
		}
	}

	return false;
}

int
main(int ac, char **av)
{
	char buf[1024];

	args.fw_type = 0;
	args.arg1 = NULL;
	args.arg2 = NULL;

	if (!parse_args(ac, av)) {
		syslog(LOG_ERR, "sshlockout: invalid argument");
		return(1);
	}

	init_iphist();

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
checkip(const char *str, const char *reason1, const char *reason2) {
	char ips[128];
	int n1;
	int n2;
	int n3;
	int n4;
	time_t t = time(NULL);

	ips[0] = '\0';

	if (sscanf(str, "%d.%d.%d.%d", &n1, &n2, &n3, &n4) == 4) {
		snprintf(ips, sizeof(ips), "%d.%d.%d.%d", n1, n2, n3, n4);
	}
	else {
		/*
		 * Check for IPv6 address (primitive way)
		 */
		int cnt = 0;
		while (str[cnt] == ':' || isxdigit(str[cnt])) {
			++cnt;
		}
		if (cnt > 0 && cnt < (int)sizeof(ips)) {
			memcpy(ips, str, cnt);
			ips[cnt] = '\0';
		}
	}

	/*
	 * We do not block localhost as is makes no sense.
	 */
	if (strcmp(ips, "127.0.0.1") == 0)
		return;
	if (strcmp(ips, "::1") == 0)
		return;

	if (strlen(ips) > 0) {

		/*
		 * Check for DoS attack. When connections from too many
		 * IP addresses come in at the same time, our hash table
		 * would overflow, so we delete the oldest entries AND
		 * block it's IP when they are younger than 10 seconds.
		 * This prevents massive attacks from arbitrary IPs.
		 */
		if (hist_count > MAXHIST + 16) {
			while (hist_count > MAXHIST) {
				iphist_t *iph = hist_base;
				int dt = (int)(t - iph->t);
				if (dt < 10) {
					syslog(LOG_ERR,
					       "Detected overflow attack, "
					       "locking out %s\n",
					       iph->ips);
					block_ip(iph->ips);
				}
				delete_iph(iph);
			}
		}

		if (insert_iph(ips, t)) {
			syslog(LOG_ERR,
			       "Detected ssh %s attempt "
			       "for %s, locking out %s\n",
			       reason1, reason2, ips);
			block_ip(ips);
		}
	}
}

static
void
checkline(char *buf)
{
	char *str;

	/*
	 * ssh login attempt with password (only hit if ssh allows
	 * password entry).  Root or admin.
	 */
	if ((str = strstr(buf, "Failed password for root from")) != NULL ||
	    (str = strstr(buf, "Failed password for admin from")) != NULL) {
		while (*str && (*str < '0' || *str > '9'))
			++str;
		checkip(str, "password login", "root or admin");
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
		if (strncmp(str, " from", 5) == 0) {
			checkip(str + 5, "password login", "an invalid user");
		}
		return;
	}

	/*
	 * ssh login attempt for non-existant user.
	 */
	if ((str = strstr(buf, "Invalid user")) != NULL) {
		str += 12;
		while (*str == ' ')
			++str;
		while (*str && *str != ' ')
			++str;
		if (strncmp(str, " from", 5) == 0) {
			checkip(str + 5, "login", "an invalid user");
		}
		return;
	}

	/*
	 * Premature disconnect in pre-authorization phase, typically an
	 * attack but require 5 attempts in an hour before cleaning it out.
	 */
	if ((str = strstr(buf, "Received disconnect from ")) != NULL &&
	    strstr(buf, "[preauth]") != NULL) {
		checkip(str + 25, "preauth", "an invalid user");
		return;
	}
}

/*
 * Insert IP record
 */
static
int
insert_iph(const char *ips, time_t t)
{
	iphist_t *ip = malloc(sizeof(*ip));
	iphist_t *scan;
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
