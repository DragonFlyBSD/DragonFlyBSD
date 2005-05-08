/*	$NetBSD: src/lib/libc/gen/isctype.c,v 1.16 2003/08/07 16:42:52 agc Exp $	*/
/*	$DragonFly: src/lib/libc/gen/isctype.c,v 1.4 2005/05/08 15:52:05 joerg Exp $ */

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

#define _ANSI_LIBRARY
#include <ctype.h>

#undef isalnum
int
isalnum(int c)
{
	return((__libc_ctype_ + 1)[c] & (_A|_D));
}

#undef isalpha
int
isalpha(int c)
{
	return((__libc_ctype_ + 1)[c] & (_A));
}

#undef isblank
int
isblank(int c)
{
	return((__libc_ctype_ + 1)[c] & _B);
}

#undef iscntrl
int
iscntrl(int c)
{
	return((__libc_ctype_ + 1)[c] & _C);
}

#undef isdigit
int
isdigit(int c)
{
	return((__libc_ctype_ + 1)[c] & _D);
}

#undef isgraph
int
isgraph(int c)
{
	return((__libc_ctype_ + 1)[c] & (_G));
}

#undef islower
int
islower(int c)
{
	return((__libc_ctype_ + 1)[c] & _L);
}

#undef isprint
int
isprint(int c)
{
	return((__libc_ctype_ + 1)[c] & (_R));
}

#undef ispunct
int
ispunct(int c)
{
	return((__libc_ctype_ + 1)[c] & _P);
}

#undef isspace
int
isspace(int c)
{
	return((__libc_ctype_ + 1)[c] & _S);
}

#undef isupper
int
isupper(int c)
{
	return((__libc_ctype_ + 1)[c] & _U);
}

#undef isxdigit
int
isxdigit(int c)
{
	return((__libc_ctype_ + 1)[c] & (_X));
}

#undef _toupper
int
_toupper(int c)
{
	return(c - 'a' + 'A');
}

#undef _tolower
int
_tolower(int c)
{
	return(c - 'A' + 'a');
}
