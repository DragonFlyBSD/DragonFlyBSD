#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/i386/gen/Makefile.inc,v 1.10.2.1 2001/02/07 00:12:45 peter Exp $
# $DragonFly: src/lib/libc/i386/gen/Makefile.inc,v 1.4 2005/03/29 19:26:19 joerg Exp $

SRCS+=	_setjmp.S alloca.S fabs.S frexp.c infinity.c isinf.c \
	ldexp.c modf.S rfork_thread.S setjmp.S sigsetjmp.S
