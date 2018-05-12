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
#include "ipfw3set.h"

void
set_toggle(int ac, char **av)
{
	int error, num;

	NEXT_ARG;

	num = atoi(av[0]);
	if (num > 0 && num < 32) {
		error = do_set_x(IP_FW_SET_TOGGLE, &num, sizeof num);
		if (error) {
			err(EX_OSERR, "getsockopt(IP_FW_SET_TOGGLE)");
		}
	} else {
		errx(EX_USAGE, "invalid set %s", *av);
	}
}

void
set_show(int ac, char **av)
{
	int i, sets = 0, len;

	len = sizeof(int);
	if (do_get_x(IP_FW_SET_GET, &sets, &len) < 0) {
		err(EX_OSERR, "getsockopt(IP_FW_SET_GET)");
	}

	printf("disable:");
	for (i = 0; i < 32; i++) {
		if (sets & (1<<i)) {
			printf(" %d", i);
		}
	}
	printf("\n");
	printf("enable:");
	for (i = 0; i < 32; i++) {
		if (!(sets & (1<<i))) {
			printf(" %d", i);
		}
	}
	printf("\n");
}

void
set_swap(int ac, char **av)
{
	int num[2], error;

	NEXT_ARG;

	if (ac != 2)
		errx(EX_USAGE, "set swap needs 2 set numbers");

	num[0] = atoi(av[0]);
	num[1] = atoi(av[1]);
	if (num[0] < 1 || num[0] > 31) {
		 errx(EX_DATAERR, "invalid set number %s", av[0]);
	}
	if (num[1] < 1 || num[1] > 31) {
		 errx(EX_DATAERR, "invalid set number %s", av[1]);
	}
	if (num[0] == num[1]) {
		 errx(EX_DATAERR, "same set numbers %s", av[0]);
	}

	error = do_set_x(IP_FW_SET_SWAP, num, sizeof(num));
	if (error) {
		err(EX_OSERR, "getsockopt(IP_FW_SET_SWAP)");
	}
}

void
set_move_rule(int ac, char **av)
{
	int num[2], error;

	NEXT_ARG;

	if (ac != 2)
		errx(EX_USAGE, "move rule needs 2 numbers");

	num[0] = atoi(av[0]);
	num[1] = atoi(av[1]);
	if (num[0] < 1 || num[0] > 65534) {
		 errx(EX_DATAERR, "invalid rule number %s", av[0]);
	}
	if (num[1] < 1 || num[1] > 31) {
		 errx(EX_DATAERR, "invalid set number %s", av[1]);
	}
	if (num[0] == num[1]) {
		 errx(EX_DATAERR, "same set numbers %s", av[0]);
	}

	error = do_set_x(IP_FW_SET_MOVE_RULE, num, sizeof(num));
	if (error) {
		err(EX_OSERR, "getsockopt(IP_FW_SET_MOVE_RULE)");
	}
}

void
set_move_set(int ac, char **av)
{
	int num[2], error;

	NEXT_ARG;

	if (ac != 2)
		errx(EX_USAGE, "move set needs 2 set numbers");

	num[0] = atoi(av[0]);
	num[1] = atoi(av[1]);
	if (num[0] < 1 || num[0] > 31) {
		 errx(EX_DATAERR, "invalid set number %s", av[0]);
	}
	if (num[1] < 1 || num[1] > 31) {
		 errx(EX_DATAERR, "invalid set number %s", av[1]);
	}
	if (num[0] == num[1]) {
		 errx(EX_DATAERR, "same set numbers %s", av[0]);
	}

	error = do_set_x(IP_FW_SET_MOVE_SET, num, sizeof(num));
	if (error) {
		err(EX_OSERR, "getsockopt(IP_FW_SET_MOVE_SET)");
	}
}

void
set_flush(int ac, char **av)
{
	int error, num;

	NEXT_ARG;

	num = atoi(av[0]);
	if (num > 0 && num < 32) {
		error = do_set_x(IP_FW_SET_FLUSH, &num, sizeof num);
		if (error) {
			err(EX_OSERR, "getsockopt(IP_FW_SET_FLUSH)");
		}
	} else {
		errx(EX_USAGE, "invalid set %s", *av);
	}
}
/*
 * ipfw3 set show
 * ipfw3 set <set num> toggle
 * ipfw3 set swap <old set num> <new set num>
 * ipfw3 set move set <old set num> to <new set num>
 * ipfw3 set move rule <rule num> to <set num>
 * ipfw3 set <set num> flush
 */
void
set_main(int ac, char **av)
{
	SWAP_ARG;
	NEXT_ARG;

	if (!ac)
		errx(EX_USAGE, "set needs command");
	if (!strncmp(*av, "show", strlen(*av)) ) {
		set_show(ac, av);
	} else if (!strncmp(*av, "swap", strlen(*av))) {
		set_swap(ac, av);
	} else if (!strncmp(*av, "move", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "set", strlen(*av)) ) {
			set_move_set(ac, av);
		} else if (!strncmp(*av, "rule", strlen(*av)) ) {
			set_move_rule(ac, av);
		} else {
			errx(EX_USAGE, "invalid move command %s", *av);
		}

	} else if (!strncmp(*av, "toggle", strlen(*av))) {
		set_toggle(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		set_flush(ac, av);
	} else {
		errx(EX_USAGE, "invalid set command %s", *av);
	}
}

