/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sys/boot/pc32/bootasmdef.c,v 1.2 2004/07/27 19:37:15 dillon Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bootasm.h"

static
void
usage(const char *arg0, int code)
{
    fprintf(stderr, "%s {BOOT0_ORIGIN,BOOT1_ORIGIN,MEM_BIOS_LADDR,BOOT2_VORIGIN}\n", arg0);
    exit(code);
}

int
main(int ac, char **av)
{
    const char *fmt;
    const char *var;

    if (ac == 1)
	usage(av[0], 1);
    if (strcmp(av[1], "-d") == 0) {
	if (ac == 2)
	    usage(av[0], 1);
	var = av[2];
	fmt = "%d\n";
    } else {
	var = av[1];
	fmt = "0x%04x\n";
    }

    if (strcmp(var, "BOOT0_ORIGIN") == 0) {
	printf(fmt, BOOT0_ORIGIN);
    } else if (strcmp(var, "BOOT1_ORIGIN") == 0) {
	printf(fmt, BOOT1_ORIGIN);
    } else if (strcmp(var, "MEM_BIOS_LADDR") == 0) {
	printf(fmt, MEM_BIOS_LADDR);
    } else if (strcmp(var, "BOOT2_VORIGIN") == 0) {
	printf(fmt, BOOT2_VORIGIN);
    } else {
	usage(av[0], 1);
    }
    return(0);
}
