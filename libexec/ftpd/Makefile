#	@(#)Makefile	8.2 (Berkeley) 4/4/94
# $FreeBSD: src/libexec/ftpd/Makefile,v 1.57 2006/06/05 15:50:34 yar Exp $

PROG=	ftpd
MAN=	ftpd.8 ftpchroot.5
SRCS=	ftpd.c ftpcmd.y logwtmpx.c popen.c

CFLAGS+=-DSETPROCTITLE -DLOGIN_CAP -DVIRTUAL_HOSTING
CFLAGS+=-I${.CURDIR}
YFLAGS=
WARNS?=	2

DPADD=	${LIBUTIL} ${LIBCRYPT}
LDADD=	-lutil -lcrypt

LSDIR=	../../bin/ls
.PATH:	${.CURDIR}/${LSDIR}
SRCS+=	ls.c cmp.c print.c util.c
CFLAGS+=-Dmain=ls_main -I${.CURDIR}/${LSDIR}
DPADD+=	${LIBM}
LDADD+=	-lm

.if !defined(NO_INET6)
CFLAGS+=-DINET6
.endif

.if !defined(NO_PAM)
CFLAGS+=-DUSE_PAM
DPADD+= ${LIBPAM}
LDADD+= ${MINUSLPAM}
LDFLAGS+= ${LDFLAGSPAM}
.endif

.if defined(NOMD5)
CFLAGS+= -DNOMD5
.else
SRCS+= site_md5.c
CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.endif

.include <bsd.prog.mk>
