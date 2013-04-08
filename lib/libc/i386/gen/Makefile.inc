#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/i386/gen/Makefile.inc,v 1.10.2.1 2001/02/07 00:12:45 peter Exp $

SRCS+=	fabs.S flt_rounds.c infinity.c \
	makecontext.c \
	_ctx_start.S _setjmp.S alloca.S \
	mcontext.S \
	rfork_thread.S setjmp.S sigsetjmp.S
