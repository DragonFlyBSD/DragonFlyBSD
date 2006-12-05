/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/vkernel/platform/machintr.c,v 1.1 2006/12/05 23:14:54 dillon Exp $
 */

#include <sys/types.h>
#include <sys/machintr.h>
#include <stdio.h>

static void dummy_intrdis(int);
static void dummy_intren(int);
static int dummy_vectorctl(int, int, int);
static int dummy_setvar(int, const void *);
static int dummy_getvar(int, void *);
static void dummy_finalize(void);

struct machintr_abi MachIntrABI = {
	MACHINTR_GENERIC,
	dummy_intrdis,
	dummy_intren,
	dummy_vectorctl,
	dummy_setvar,
	dummy_getvar,
	dummy_finalize
};

static void
dummy_intrdis(int)
{
}

static void
dummy_intren(int)
{
}

static int
dummy_vectorctl(int, int, int)
{
	return (EOPNOTSUPP);
}

static int
dummy_setvar(int, const void *)
{
	return (ENOENT);
}

static int
dummy_getvar(int, void *)
{
	return (ENOENT);
}

static void
dummy_finalize(void)
{
}

