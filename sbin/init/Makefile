#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/sbin/init/Makefile,v 1.20.2.4 2001/08/01 06:37:01 obrien Exp $
# $DragonFly: src/sbin/init/Makefile,v 1.5 2006/10/17 00:55:41 pavalos Exp $

PROG=	init
MAN=	init.8
MLINKS=	init.8 securelevel.8
BINMODE=500
INSTALLFLAGS= -b -B.bak
.if !defined(NOFSCHG)
INSTALLFLAGS+= -fschg
.endif
CFLAGS+=-DDEBUGSHELL -DSECURE -DLOGIN_CAP -DCOMPAT_SYSV_INIT -DSUPPORT_UTMPX
DPADD=	${LIBUTIL} ${LIBCRYPT}
LDADD=	-lutil -lcrypt

NOSHARED=yes

.include <bsd.prog.mk>
