#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xinstall/Makefile,v 1.11.2.4 2001/09/24 15:44:05 ru Exp $

PROG=		xinstall
PROGNAME=	install
SRCS=		xinstall.c
MAN=		install.1

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.else
.PATH:          ${.CURDIR}/../../usr.sbin/mtree
SRCS+=		getid.c
CFLAGS+=        -DHAVE_NETDB_H -I${.CURDIR}/../../usr.sbin/mtree
.endif

.if ${WORLD_VERSION} < 500302 # pwcache(3)/vis(3) upgrades came in about here
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=  pwcache.c
#CFLAGS+=-include ${.CURDIR}/../../include/pwcache.h
CFLAGS+=-I${.CURDIR}/../../lib/libc/gen
CFLAGS+=-DEMBED_LIB_SRC
.endif

.include <bsd.prog.mk>
