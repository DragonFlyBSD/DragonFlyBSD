# $FreeBSD: src/secure/libexec/ssh-keysign/Makefile,v 1.1.2.2 2003/02/03 17:31:12 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	ssh-keysign
SRCS=	ssh-keysign.c
SRCS+=	readconf.c uidswap.c
MAN=	ssh-keysign.8
.if defined(ENABLE_SUID_SSH)
BINMODE=4511
.endif

WARNS=	3

.if defined(XAUTH_PATH)
CFLAGS+= -DXAUTH_PATH=\"${XAUTH_PATH}\"
.endif

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
