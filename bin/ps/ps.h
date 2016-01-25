/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)ps.h	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/bin/ps/ps.h,v 1.7 1999/08/27 23:14:52 peter Exp $
 * $DragonFly: src/bin/ps/ps.h,v 1.9 2007/02/01 10:33:25 corecode Exp $
 */

#define	UNLIMITED	0	/* unlimited terminal width */
enum type { CHAR, UCHAR, SHORT, USHORT, INT, UINT, LONG, ULONG, KPTR };

#define KI_PROC(ki, f) ((ki)->ki_proc->kp_ ## f)
#define KI_LWP(ki, f) ((ki)->ki_proc->kp_lwp.kl_ ## f)

typedef struct kinfo {
	struct kinfo_proc *ki_proc;	/* proc info structure */
	const char *ki_args;		/* exec args */
	const char *ki_env;		/* environment */
	int	ki_indent;		/* used by dochain */
	struct kinfo *ki_hnext;		/* used by dochain */
	struct kinfo *ki_cbase;		/* used by dochain */
	struct kinfo **ki_ctailp;	/* used by dochain */
	struct kinfo *ki_cnext;		/* used by dochain */
	struct kinfo *ki_parent;	/* used by dochain */
} KINFO;

/* Variables. */
struct varent {
	STAILQ_ENTRY(varent) link;
	const struct var *var;
	const char *header;
	int	    width;		/* printing width */
	int	    dwidth;		/* dynamic printing width */
};

typedef struct var {
	const char  *name;		/* name(s) of variable */
	const char  *header;		/* default header */
	const char  *alias;		/* aliases */
#define	COMM	    0x01		/* needs exec arguments and environment (XXX) */
#define	LJUST	    0x02		/* left adjust on output (trailing blanks) */
#define	USER	    0x04		/* needs user structure */
#define	DSIZ	    0x08		/* field size is dynamic*/
	u_int	    flag;
					/* output routine */
	void	    (*oproc) (const struct kinfo *, const struct varent *);
					/* sizing routine*/
	int	    (*sproc) (const struct kinfo *);
	short	    width;		/* printing width */
	/*
	 * The following (optional) elements are hooks for passing information
	 * to the generic output routines: pvar, evar, uvar (those which print
	 * simple elements from well known structures: proc, eproc, usave)
	 */
	int	    off;		/* offset in structure */
	enum	    type type;		/* type of element */
	const char  *fmt;		/* printf format */
	const char  *time;		/* time format */
	/*
	 * glue to link selected fields together
	 */
} VAR;

#include "extern.h"
