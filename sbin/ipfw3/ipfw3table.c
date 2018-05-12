/*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
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

#include <net/ipfw3/ip_fw3.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_nat/ip_fw3_nat.h>
#include <net/dummynet3/ip_dummynet3.h>

#include "ipfw3.h"
#include "ipfw3table.h"

int
lookup_host (char *host, struct in_addr *ipaddr)
{
	struct hostent *he;

	if (!inet_aton(host, ipaddr)) {
		if ((he = gethostbyname(host)) == NULL)
			return(-1);
		*ipaddr = *(struct in_addr *)he->h_addr_list[0];
	}
	return(0);
}


void
table_append(int ac, char *av[])
{
	struct ipfw_ioc_table tbl;
	char *p;
	int size;

	NEXT_ARG;
	if (isdigit(**av))
		tbl.id = atoi(*av);
	else
		errx(EX_USAGE, "table id `%s' invalid", *av);

	if (tbl.id < 0 || tbl.id > IPFW_TABLES_MAX - 1)
		errx(EX_USAGE, "table id `%d' invalid", tbl.id);

	NEXT_ARG;
	if (strcmp(*av, "ip") == 0)
		tbl.type = 1;
	else if (strcmp(*av, "mac") == 0)
		tbl.type = 2;
	else
		errx(EX_USAGE, "table type `%s' not supported", *av);

	NEXT_ARG;
        if (tbl.type == 1) { /* table type ipv4 */
                struct ipfw_ioc_table_ip_entry ip_ent;
                if (!ac)
                        errx(EX_USAGE, "IP address required");

                p = strchr(*av, '/');
                if (p) {
                        *p++ = '\0';
                        ip_ent.masklen = atoi(p);
                        if (ip_ent.masklen > 32)
                                errx(EX_DATAERR, "bad width ``%s''", p);
                } else {
                        ip_ent.masklen = 32;
                }

                if (lookup_host(*av, (struct in_addr *)&ip_ent.addr) != 0)
                        errx(EX_NOHOST, "hostname ``%s'' unknown", *av);

                tbl.ip_ent[0] = ip_ent;
                size = sizeof(tbl) + sizeof(ip_ent);
        } else if (tbl.type == 2) { /* table type mac */
                struct ipfw_ioc_table_mac_entry mac_ent;
                if (!ac)
                        errx(EX_USAGE, "MAC address required");

                mac_ent.addr = *ether_aton(*av);
                tbl.mac_ent[0] = mac_ent;
                size = sizeof(tbl) + sizeof(mac_ent);
        }
	if (do_set_x(IP_FW_TABLE_APPEND, &tbl, size) < 0 )
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_APPEND) "
			"table `%d' append `%s' failed", tbl.id, *av);
}

void
table_remove(int ac, char *av[])
{
	struct ipfw_ioc_table tbl;
	struct ipfw_ioc_table_ip_entry ip_ent;
	char *p;
	int size;

	NEXT_ARG;
	if (isdigit(**av))
		tbl.id = atoi(*av);
	else
		errx(EX_USAGE, "table id `%s' invalid", *av);

	if (tbl.id < 0 || tbl.id > IPFW_TABLES_MAX - 1)
		errx(EX_USAGE, "table id `%d' invalid", tbl.id);

	NEXT_ARG;
	if (strcmp(*av, "ip") == 0)
		tbl.type = 1;
	else if (strcmp(*av, "mac") == 0)
		tbl.type = 2;
	else
		errx(EX_USAGE, "table type `%s' not supported", *av);

	NEXT_ARG;
	if (!ac)
		errx(EX_USAGE, "IP address required");
	p = strchr(*av, '/');
	if (p) {
		*p++ = '\0';
		ip_ent.masklen = atoi(p);
		if (ip_ent.masklen > 32)
			errx(EX_DATAERR, "bad width ``%s''", p);
	} else {
		ip_ent.masklen = 32;
	}

	if (lookup_host(*av, (struct in_addr *)&ip_ent.addr) != 0)
		errx(EX_NOHOST, "hostname ``%s'' unknown", *av);

	tbl.ip_ent[0] = ip_ent;
	size = sizeof(tbl) + sizeof(ip_ent);
	if (do_set_x(IP_FW_TABLE_REMOVE, &tbl, size) < 0 ) {
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_REMOVE) "
			"table `%d' append `%s' failed", tbl.id, *av);
	}
}

void
table_flush(int ac, char *av[])
{
	struct ipfw_ioc_table ioc_table;
	struct ipfw_ioc_table *t = &ioc_table;

	NEXT_ARG;
	if (isdigit(**av)) {
		t->id = atoi(*av);
		if (t->id < 0 || t->id > IPFW_TABLES_MAX - 1)
			errx(EX_USAGE, "table id `%d' invalid", t->id);
	} else {
		errx(EX_USAGE, "table id `%s' invalid", *av);
	}
	if (do_set_x(IP_FW_TABLE_FLUSH, t, sizeof(struct ipfw_ioc_table)) < 0 )
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_FLUSH) "
					"table `%s' flush failed", *av);
}

void
table_list(int ac, char *av[])
{
	struct ipfw_ioc_table *ioc_table;
	int i, count, nbytes, nalloc = 1024;
	void *data = NULL;
	NEXT_ARG;
	if (strcmp(*av, "list") == 0) {
		nbytes = nalloc;
		while (nbytes >= nalloc) {
			nalloc = nalloc * 2 ;
			nbytes = nalloc;
			if ((data = realloc(data, nbytes)) == NULL)
				err(EX_OSERR, "realloc");
			if (do_get_x(IP_FW_TABLE_LIST, data, &nbytes) < 0)
				err(EX_OSERR, "do_get_x(IP_FW_TABLE_LIST)");
		}
		ioc_table = (struct ipfw_ioc_table *)data;
		count = nbytes / sizeof(struct ipfw_ioc_table);
		for (i = 0; i < count; i++, ioc_table++) {
			if (ioc_table->type > 0) {
				printf("table %d",ioc_table->id);
				if (ioc_table->type == 1)
					printf(" type ip");
				else if (ioc_table->type == 2)
					printf(" type mac");
				printf(" count %d",ioc_table->count);
				if (strlen(ioc_table->name) > 0)
					printf(" name %s",ioc_table->name);
				printf("\n");

			}
		}
	} else {
		errx(EX_USAGE, "ipfw3 table `%s' delete invalid", *av);
	}
}

void
table_print(struct ipfw_ioc_table * tbl)
{
	int i;
        if (tbl->type == 0)
                errx(EX_USAGE, "table %d is not in use", tbl->id);

        printf("table %d", tbl->id);
        if (tbl->type == 1)
                printf(" type ip");
        else if (tbl->type == 2)
                printf(" type mac");

        printf(" count %d", tbl->count);
	if (strlen(tbl->name) > 0)
		printf(" name %s", tbl->name);

	printf("\n");

        if (tbl->type == 1) {
                struct ipfw_ioc_table_ip_entry *ip_ent;
                ip_ent = tbl->ip_ent;
                for (i = 0; i < tbl->count; i++) {
                        printf("%s", inet_ntoa(*(struct in_addr *)&ip_ent->addr));
                        printf("/%d ", ip_ent->masklen);
                        printf("\n");
                        ip_ent++;
                }
        } else if (tbl->type == 2) {
                struct ipfw_ioc_table_mac_entry *mac_ent;
                mac_ent = tbl->mac_ent;
                for (i = 0; i < tbl->count; i++) {
                        printf("%s", ether_ntoa(&mac_ent->addr));
                        printf("\n");
                        mac_ent++;
                }
        }
}

void
table_show(int ac, char *av[])
{
	int nbytes, nalloc = 1024;
	void *data = NULL;
	NEXT_ARG;
	if (isdigit(**av)) {
		nbytes = nalloc;
		while (nbytes >= nalloc) {
			nalloc = nalloc * 2 + 256;
			nbytes = nalloc;
			if (data == NULL) {
				if ((data = malloc(nbytes)) == NULL) {
					err(EX_OSERR, "malloc");
				}
			} else if ((data = realloc(data, nbytes)) == NULL) {
				err(EX_OSERR, "realloc");
			}
			/* store table id in the header of data */
			int *head = (int *)data;
			*head = atoi(*av);
			if (*head < 0 || *head > IPFW_TABLES_MAX - 1)
				errx(EX_USAGE, "table id `%d' invalid", *head);
			if (do_get_x(IP_FW_TABLE_SHOW, data, &nbytes) < 0)
				err(EX_OSERR, "do_get_x(IP_FW_TABLE_LIST)");
			struct ipfw_ioc_table *tbl;
			tbl = (struct ipfw_ioc_table *)data;
			table_print(tbl);
		}
	} else {
		errx(EX_USAGE, "ipfw3 table `%s' show invalid", *av);
	}
}

void
table_create(int ac, char *av[])
{
	struct ipfw_ioc_table ioc_table;
	struct ipfw_ioc_table *t = &ioc_table;

	NEXT_ARG;
	if (ac < 2)
		errx(EX_USAGE, "table parameters invalid");
	if (isdigit(**av)) {
		t->id = atoi(*av);
		if (t->id < 0 || t->id > IPFW_TABLES_MAX - 1)
			errx(EX_USAGE, "table id `%d' invalid", t->id);
	} else {
		errx(EX_USAGE, "table id `%s' invalid", *av);
	}
	NEXT_ARG;
	if (strcmp(*av, "ip") == 0)
		t->type = 1;
	else if (strcmp(*av, "mac") == 0)
		t->type = 2;
	else
		errx(EX_USAGE, "table type `%s' not supported", *av);

	NEXT_ARG;
	memset(t->name, 0, IPFW_TABLE_NAME_LEN);
	if (ac == 2 && strcmp(*av, "name") == 0) {
		NEXT_ARG;
		if (strlen(*av) < IPFW_TABLE_NAME_LEN) {
			strncpy(t->name, *av, strlen(*av));
		} else {
			errx(EX_USAGE, "table name `%s' too long", *av);
		}
	} else if (ac == 1) {
		errx(EX_USAGE, "table `%s' invalid", *av);
	}

	if (do_set_x(IP_FW_TABLE_CREATE, t, sizeof(struct ipfw_ioc_table)) < 0)
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_CREATE) "
					"table `%d' in use", t->id);
}

void
table_delete(int ac, char *av[])
{
	struct ipfw_ioc_table ioc_table;
	struct ipfw_ioc_table *t = &ioc_table;

	NEXT_ARG;
	if (isdigit(**av)) {
		t->id = atoi(*av);
		if (t->id < 0 || t->id > IPFW_TABLES_MAX - 1)
			errx(EX_USAGE, "table id `%d' invalid", t->id);
	} else {
		errx(EX_USAGE, "table id `%s' invalid", *av);
	}
	if (t->id < 0 || t->id > IPFW_TABLES_MAX - 1)
		errx(EX_USAGE, "table id `%d' invalid", t->id);

	if (do_set_x(IP_FW_TABLE_DELETE, t, sizeof(struct ipfw_ioc_table)) < 0)
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_DELETE) "
					"table `%s' delete failed", *av);
}

void
table_test(int ac, char *av[])
{
	struct ipfw_ioc_table tbl;
	int size;

	NEXT_ARG;
	if (isdigit(**av))
		tbl.id = atoi(*av);
	else
		errx(EX_USAGE, "table id `%s' invalid", *av);

	if (tbl.id < 0 || tbl.id > IPFW_TABLES_MAX - 1)
		errx(EX_USAGE, "table id `%d' invalid", tbl.id);

	NEXT_ARG;
	if (strcmp(*av, "ip") == 0)
		tbl.type = 1;
	else if (strcmp(*av, "mac") == 0)
		tbl.type = 2;
	else
		errx(EX_USAGE, "table type `%s' not supported", *av);

	NEXT_ARG;
        if (tbl.type == 1) { /* table type ipv4 */
                struct ipfw_ioc_table_ip_entry ip_ent;
                if (lookup_host(*av, (struct in_addr *)&ip_ent.addr) != 0)
                        errx(EX_NOHOST, "hostname ``%s'' unknown", *av);

                tbl.ip_ent[0] = ip_ent;
                size = sizeof(tbl) + sizeof(ip_ent);
        } else if (tbl.type == 2) { /* table type mac */
                struct ipfw_ioc_table_mac_entry mac_ent;
                if (!ac)
                        errx(EX_USAGE, "MAC address required");

                mac_ent.addr = *ether_aton(*av);
                tbl.mac_ent[0] = mac_ent;
                size = sizeof(tbl) + sizeof(mac_ent);
        }
	if (do_set_x(IP_FW_TABLE_TEST, &tbl, size) < 0 ) {
		printf("NO, %s not exists in table %d\n", *av, tbl.id);
	} else {
		printf("YES, %s exists in table %d\n", *av, tbl.id);
	}
}

static void
table_rename(int ac, char *av[])
{
	struct ipfw_ioc_table tbl;
	int size;

	bzero(&tbl, sizeof(tbl));
	NEXT_ARG;
	if (isdigit(**av))
		tbl.id = atoi(*av);
	else
		errx(EX_USAGE, "table id `%s' invalid", *av);

	if (tbl.id < 0 || tbl.id > IPFW_TABLES_MAX - 1)
		errx(EX_USAGE, "table id `%d' invalid", tbl.id);

	NEXT_ARG;
	strlcpy(tbl.name, *av, IPFW_TABLE_NAME_LEN);
	size = sizeof(tbl);
	if (do_set_x(IP_FW_TABLE_RENAME, &tbl, size) < 0 )
		errx(EX_USAGE, "do_set_x(IP_FW_TABLE_RENAME) "
					"table `%d' not in use", tbl.id);
}

void
table_main(int ac, char **av)
{
	if (!strncmp(*av, "append", strlen(*av))) {
		table_append(ac, av);
	} else if (!strncmp(*av, "remove", strlen(*av))) {
		table_remove(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		table_flush(ac, av);
	} else if (!strncmp(*av, "list", strlen(*av))) {
		table_list(ac, av);
	} else if (!strncmp(*av, "show", strlen(*av))) {
		table_show(ac, av);
	} else if (!strncmp(*av, "type", strlen(*av))) {
		table_create(ac, av);
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		table_delete(ac, av);
	} else if (!strncmp(*av, "test", strlen(*av))) {
		table_test(ac,av);
	} else if (!strncmp(*av, "name", strlen(*av))) {
		table_rename(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw table command `%s'", *av);
	}
}

