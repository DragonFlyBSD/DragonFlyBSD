#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/su/Makefile,v 1.29.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/usr.bin/su/Makefile,v 1.3 2003/08/05 07:45:44 asmodai Exp $

PROG=	su
SRCS=	su.c

COPTS+=	-DLOGIN_CAP -DSKEY
DPADD=	${LIBUTIL} ${LIBSKEY} ${LIBMD} ${LIBCRYPT}
LDADD=	-lutil -lskey -lmd -lcrypt

.if defined(WHEELSU)
COPTS+=	-DWHEELSU
.endif
CFLAGS+= -Wall

.if defined(MAKE_KERBEROS5) && !defined(NOCRYPT) && !defined(NO_OPENSSL)
CFLAGS+=-DKERBEROS5
DPADD+=	${LIBKRB5} ${LIBASN1} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR} \
	${LIBROKEN}
LDADD+=	-lkrb5 -lasn1 -lcrypto -lcrypt -lcom_err \
	-L${.OBJDIR}/../../../../kerberos5/lib/libroken -lroken
DISTRIBUTION=	krb5
.endif

BINMODE=4555
INSTALLFLAGS=-fschg

.include <bsd.prog.mk>
