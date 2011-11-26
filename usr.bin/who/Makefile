#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $DragonFly: src/usr.bin/who/Makefile,v 1.3 2007/08/27 16:51:01 pavalos Exp $

PROG=	who

SRCS+=	who.c utmpentry.c

CFLAGS+=	-DSUPPORT_UTMPX -DSUPPORT_UTMP

.include <bsd.prog.mk>
