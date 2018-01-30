#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/inetd/Makefile,v 1.18.2.4 2001/12/19 04:49:11 dd Exp $

PROG=	inetd
SRCS=	inetd.c builtins.c
MAN=	inetd.8
MLINKS=	inetd.8 inetd.conf.5

CFLAGS+=-DINET6 -DLOGIN_CAP
#CFLAGS+= -DSANITY_CHECK

DPADD+=	${LIBUTIL} ${LIBWRAP}
LDADD+=	-lutil -lwrap

# smth very strange with "sep = tcpmux(ctrl)"
NO_WCAST_FUNCTION_TYPE=

.include <bsd.prog.mk>
