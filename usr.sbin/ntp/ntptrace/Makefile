# $FreeBSD: src/usr.sbin/ntp/ntptrace/Makefile,v 1.2.2.1 2001/12/21 17:42:35 roberto Exp $
# $DragonFly: src/usr.sbin/ntp/ntptrace/Attic/Makefile,v 1.2 2003/06/17 04:29:59 dillon Exp $

.PATH: ${.CURDIR}/../../../contrib/ntp/ntptrace

PROG=	ntptrace
NOMAN=	yes
SRCS= 	ntptrace.c version.c

CFLAGS+= -I${.CURDIR}/../../../contrib/ntp/include -I${.CURDIR}/../

DPADD=	${LIBNTP}
LDADD=	${LIBNTP}

CLEANFILES+= .version version.c

version.c: 
	sh -e ${.CURDIR}/../scripts/mkver ntptrace

.include <bsd.prog.mk>
