#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xinstall/Makefile,v 1.11.2.4 2001/09/24 15:44:05 ru Exp $

PROG=		xinstall
PROGNAME=	install
SRCS=		xinstall.c getid.c
MAN=		install.1

.PATH:          ${.CURDIR}/../../usr.sbin/mtree
CFLAGS+=        -DHAVE_NETDB_H -I${.CURDIR}/../../usr.sbin/mtree

.if defined(BOOTSTRAPPING)
.if ${WORLD_VERSION} < 500302 # pwcache(3) upgrades came in about here
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=	pwcache.c
CFLAGS+=-I${.CURDIR}/../../lib/libc/include
CFLAGS+=-include ${.CURDIR}/../../include/grp.h
CFLAGS+=-include ${.CURDIR}/../../include/pwd.h
.endif
.endif

.if ${WORLD_VERSION} < 500302 # pwcache(3)/vis(3) upgrades came in about here
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=  pwcache.c
#CFLAGS+=-include ${.CURDIR}/../../include/pwcache.h
CFLAGS+=-I${.CURDIR}/../../lib/libc/gen
CFLAGS+=-DEMBED_LIB_SRC
.endif

.include <bsd.prog.mk>
