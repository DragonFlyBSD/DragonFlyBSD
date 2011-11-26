# $FreeBSD: src/usr.sbin/pppctl/Makefile,v 1.5.2.1 2001/04/25 12:10:34 ru Exp $
# $DragonFly: src/usr.sbin/pppctl/Makefile,v 1.3 2008/10/29 18:18:04 swildner Exp $

PROG=	pppctl
MAN=	pppctl.8
WARNS?=	2
LDADD+= -ledit -ltermcap
DPADD+= ${LIBEDIT} ${LIBTERMCAP}

.include <bsd.prog.mk>
