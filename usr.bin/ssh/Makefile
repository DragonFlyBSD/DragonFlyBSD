# $FreeBSD: src/secure/usr.bin/ssh/Makefile,v 1.4.2.8 2003/02/03 17:31:12 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1
WARNS?=	2

SRCS=	ssh.c
SRCS+=	clientloop.c mux.c readconf.c \
	sshconnect.c sshconnect2.c sshtty.c

.if defined(XAUTH_PATH)
CFLAGS+= -DXAUTH_PATH=\"${XAUTH_PATH}\"
.endif

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
