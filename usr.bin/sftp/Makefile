# $FreeBSD: src/secure/usr.bin/sftp/Makefile,v 1.1.2.3 2002/07/03 22:20:30 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	sftp
SRCS=	sftp.c
SRCS+=	progressmeter.c sftp-client.c sftp-common.c sftp-glob.c
WARNS?=	3

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh

DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz

CFLAGS+=	${PRIVATELIB_CFLAGS}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

DPADD+=		${LIBEDIT} ${LIBNCURSES}
LDADD+=		-lprivate_edit -lprivate_ncurses

.include <bsd.prog.mk>
