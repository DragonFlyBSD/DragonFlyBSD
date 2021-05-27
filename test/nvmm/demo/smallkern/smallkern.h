/*
 * Copyright (c) 2018 The NetBSD Foundation, Inc. All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/stdbool.h>
#include <machine/pte.h>

#include "pdir.h"

#define MM_PROT_READ	0x00
#define MM_PROT_WRITE	0x01
#define MM_PROT_EXECUTE	0x02

#define ASSERT(a) if (!(a)) fatal("ASSERT");
#define memset(d, v, l) __builtin_memset(d, v, l)
#define memcpy(d, v, l) __builtin_memcpy(d, v, l)
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef uint64_t pt_entry_t;
typedef uint64_t pd_entry_t;
typedef uint64_t pte_prot_t;
#define WHITE_ON_BLACK 0x07
#define RED_ON_BLACK 0x04
#define GREEN_ON_BLACK 0x02

/* -------------------------------------------------------------------------- */

/* console.c */
void init_cons();
void print_ext(int, char *);
void print(char *);
void print_state(bool, char *);
void print_banner();

/* locore.S */
void lidt(void *);
uint64_t rdtsc();
void jump_kernel();

/* main.c */
void fatal(char *);

