/*	$NetBSD: src/lib/libc/gen/ctype_.c,v 1.16 2003/08/07 16:42:46 agc Exp $	*/
/*	$DragonFly: src/lib/libc/gen/ctype.c,v 1.5 2005/09/17 14:39:44 joerg Exp $ */

/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define _CTYPE_PRIVATE

#include <sys/types.h>
#include <ctype.h>
#include <limits.h>

#define	_U	_CTYPEMASK_U
#define	_L	_CTYPEMASK_L
#define	_D	_CTYPEMASK_D
#define	_S	_CTYPEMASK_S
#define	_P	_CTYPEMASK_P
#define	_C	_CTYPEMASK_C
#define	_X	_CTYPEMASK_X
#define	_B	_CTYPEMASK_B
#define	_A	_CTYPEMASK_A
#define	_G	_CTYPEMASK_G
#define	_R	_CTYPEMASK_R

const uint16_t __libc_C_ctype_[1 + _CTYPE_NUM_CHARS] = {
	0,
/*00*/	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C|_S|_B,	_C|_S,		_C|_S,
	_C|_S,		_C|_S,		_C,		_C,
/*10*/	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
	_C,		_C,		_C,		_C,
/*20*/	_S|_B|_R,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
/*30*/	_D|_R|_G|_X,	_D|_R|_G|_X,	_D|_R|_G|_X,	_D|_R|_G|_X,
	_D|_R|_G|_X,	_D|_R|_G|_X,	_D|_R|_G|_X,	_D|_R|_G|_X,
	_D|_R|_G|_X,	_D|_R|_G|_X,	_P|_R|_G,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
/*40*/	_P|_R|_G,	_U|_X|_R|_G|_A,	_U|_X|_R|_G|_A,	_U|_X|_R|_G|_A,
	_U|_X|_R|_G|_A,	_U|_X|_R|_G|_A,	_U|_X|_R|_G|_A,	_U|_R|_G|_A,
	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,
	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,
/*50*/	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,
	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,
	_U|_R|_G|_A,	_U|_R|_G|_A,	_U|_R|_G|_A,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,
/*60*/	_P|_R|_G,	_L|_X|_R|_G|_A,	_L|_X|_R|_G|_A,	_L|_X|_R|_G|_A,
	_L|_X|_R|_G|_A,	_L|_X|_R|_G|_A,	_L|_X|_R|_G|_A,	_L|_R|_G|_A,
	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,
	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,
/*70*/	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,
	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,
	_L|_R|_G|_A,	_L|_R|_G|_A,	_L|_R|_G|_A,	_P|_R|_G,
	_P|_R|_G,	_P|_R|_G,	_P|_R|_G,	_C
};

const uint16_t *__libc_ctype_ = __libc_C_ctype_;
