#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/amd64/gen/Makefile.inc,v 1.24 2003/10/13 20:32:33 alc Exp $

SRCS+=	_setjmp.S rfork_thread.S setjmp.S sigsetjmp.S \
	makecontext.c nan.c \
	qcontext.S quickcontext.c \
	fabs.S flt_rounds.c fpgetmask.c fpgetprec.c fpgetround.c \
	fpgetsticky.c fpsetmask.c fpsetprec.c fpsetround.c infinity.c

SRCS+=	isfinitel.c isnanl.c isnormall.c signbitl.c
