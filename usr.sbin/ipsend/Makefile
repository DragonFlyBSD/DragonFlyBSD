# $FreeBSD: src/usr.sbin/ipsend/Makefile,v 1.8.2.3 2001/11/14 14:15:05 guido Exp $
# $DragonFly: src/usr.sbin/ipsend/Makefile,v 1.8 2004/07/28 00:22:37 hmp Exp $

.PATH:	${.CURDIR}/../../contrib/ipfilter/ipsend \
	${.CURDIR}/../../contrib/ipfilter/iplang \
	${.CURDIR}/../../contrib/ipfilter

PROG=	ipsend
SRCS=	ipsend.c ip.c ipsopt.c sbpf.c sock.c 44arp.c iplang_y.y iplang_l.l \
	y.tab.h
MAN=	ipsend.1 ipsend.5

CFLAGS+=-DDOSOCKET -DIPL_NAME=\"/dev/ipl\" -DUSE_INET6
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter/netinet
CFLAGS+=-I${.CURDIR}/../../sys/contrib/ipfilter
CFLAGS+=-I. -I${.CURDIR}/../../contrib/ipfilter/ipsend \
	-I${.CURDIR}/../../contrib/ipfilter/iplang \
	-I${.CURDIR}/../../contrib/ipfilter

#Define __FreeBSD__ as is needed when building with gcc3.
CFLAGS+=-D__FreeBSD__

DPADD=	${LIBL}
LDADD=	-ll

.include <bsd.prog.mk>
