# $FreeBSD: src/secure/libexec/sftp-server/Makefile,v 1.1.2.3 2002/07/03 22:13:22 des Exp $

PROG=   sftp-server
SRCS=   sftp-server.c sftp-common.c
MAN=	sftp-server.8
CFLAGS+=-I${SSHDIR}

DPADD+=	${LIBSSH} ${LIBCRYPTO}
LDADD+=	-lssh -lcrypto

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
