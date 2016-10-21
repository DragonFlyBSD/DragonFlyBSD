#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/chflags/Makefile,v 1.8.2.2 2001/08/01 23:09:18 obrien Exp $

NOSHARED?=yes

PROG=	chflags

.if defined(BOOTSTRAPPING)
# Keep version check up to date.
. if ${WORLD_VERSION} <= 400103
CFLAGS+=	-I${.CURDIR} -include chflags-compat.h
. endif
.endif

.include <bsd.prog.mk>
