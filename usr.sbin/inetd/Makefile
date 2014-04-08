#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/inetd/Makefile,v 1.18.2.4 2001/12/19 04:49:11 dd Exp $
# $DragonFly: src/usr.sbin/inetd/Makefile,v 1.4 2008/05/23 23:40:59 swildner Exp $

PROG=	inetd
SRCS=	inetd.c builtins.c
MAN=	inetd.8
MLINKS=	inetd.8 inetd.conf.5

CFLAGS+= -DLOGIN_CAP
#CFLAGS+= -DSANITY_CHECK

DPADD+=	${LIBUTIL} ${LIBWRAP}
LDADD+=	-lutil -lwrap
CFLAGS+=-DINET6 -DIPSEC
DPADD+=	${LIBIPSEC}
LDADD+=	-lipsec
.include <bsd.prog.mk>
