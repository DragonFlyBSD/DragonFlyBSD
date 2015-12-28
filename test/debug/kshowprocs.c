/*
 * KSHOWPROCS.C
 *
 * cc kshowprocs.c -o /usr/local/bin/kshowprocs -g -O0 -pipe -lkvm
 *
 * Dump kernel processes
 *
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete <tuxillo@quantumachine.net>
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <kvm.h>
#include <err.h>
#include <nlist.h>
#include <getopt.h>

#include <sys/user.h>
#include <sys/sysctl.h>

int debugopt;
int verboseopt;

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    struct kinfo_proc *kp;
    kvm_t *kd;
    int ch;
    int i;
    int nprocs;

    while ((ch = getopt(ac, av, "M:N:v")) != -1) {
	switch(ch) {
	case 'v':
	    ++verboseopt;
	    break;
	case 'M':
	    corefile = optarg;
	    break;
	case 'N':
	    sysfile = optarg;
	    break;
	default:
	    fprintf(stderr, "%s [-M core] [-N system]\n", av[0]);
	    exit(1);
	}
    }
    ac -= optind;
    av += optind;

    if ((kd = kvm_open(sysfile, corefile, NULL, O_RDONLY, "kvm:")) == NULL) {
	perror("kvm_open");
	exit(1);
    }

    if ((kp = kvm_getprocs(kd, KERN_PROC_ALL, 0, &nprocs)) == NULL)
	errx(1, "%s", kvm_geterr(kd));

    fprintf(stdout, "%-6s %-6s %-20s %-10s %-5s\n",
	"PID",
	"PPID",
	"COMMAND",
	"LOGIN",
	"NICE");

    for (i = 0; i < nprocs; i++) {
	    fprintf(stdout, "%-6d %-6d %-20s %-10s %-5d\n",
		kp[i].kp_pid,
		kp[i].kp_ppid,
		kp[i].kp_comm,
		kp[i].kp_login,
		kp[i].kp_nice);
    }

    kvm_close(kd);

    return 0;

}
