# From:	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/login/Makefile,v 1.31.2.2 2001/07/16 07:02:39 markm Exp $
# $DragonFly: src/usr.bin/login/Makefile,v 1.3 2004/11/17 19:16:01 dillon Exp $

PROG=	login
SRCS=	login.c login_access.c login_fbtab.c
MAN=	login.1 login.access.5

CFLAGS+=-Wall -DLOGIN_ACCESS -DLOGALL

DPADD=	${LIBUTIL} ${LIBCRYPT}
LDADD=	-lutil -lcrypt

.if !defined(NOPAM)
CFLAGS+= -DUSE_PAM
DPADD+=	${LIBPAM}
LDADD+=	${MINUSLPAM}
.endif

BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif
NEED_LIBNAMES= yes

.include <bsd.prog.mk>
