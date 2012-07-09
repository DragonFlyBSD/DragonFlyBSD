#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/i386/gen/Makefile.inc,v 1.10.2.1 2001/02/07 00:12:45 peter Exp $

SRCS+=	fpclassifyl.c infinityl.c isfinitel.c isinfl.c isnanl.c \
	ldexp.c makecontext.c nanf.c signbitl.c \
	_ctx_start.S _setjmp.S alloca.S fabs.S flt_rounds.S fpgetmask.S \
	fpgetround.S \
	fpgetsticky.S fpsetmask.S fpsetround.S fpsetsticky.S mcontext.S modf.S \
	rfork_thread.S setjmp.S sigsetjmp.S

