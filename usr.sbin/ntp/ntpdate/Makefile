# $FreeBSD: src/usr.sbin/ntp/ntpdate/Makefile,v 1.2.2.1 2001/12/21 17:42:34 roberto Exp $
# $DragonFly: src/usr.sbin/ntp/ntpdate/Attic/Makefile,v 1.2 2003/06/17 04:29:58 dillon Exp $

.PATH: ${.CURDIR}/../../../contrib/ntp/ntpdate

PROG=	ntpdate
NOMAN=	yes
SRCS=	ntpdate.c version.c

CFLAGS+= -I${.CURDIR}/../../../contrib/ntp/include -I${.CURDIR}/../

DPADD=	${LIBNTP}
LDADD=	${LIBNTP}

CLEANFILES+= .version version.c

version.c: 
	sh -e ${.CURDIR}/../scripts/mkver ntpdate

.include <bsd.prog.mk>
