#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/amd64/gen/Makefile.inc,v 1.24 2003/10/13 20:32:33 alc Exp $
# $DragonFly: src/lib/libc/amd64/gen/Makefile.inc,v 1.3 2006/07/27 00:46:57 corecode Exp $

SRCS+=	_setjmp.S rfork_thread.S setjmp.S sigsetjmp.S \
	fabs.S modf.S \
	fpclassifyl.c nanf.c signbitl.c flt_rounds.c \
	frexp.c infinityl.c isinfl.c isfinitel.c isnanl.c \
	ldexp.c fpclassifyl.c \
	fpgetmask.S fpsetmask.S fpgetround.S fpsetround.S \
	fpgetsticky.S fpsetsticky.S
