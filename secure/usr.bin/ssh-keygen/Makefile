# $FreeBSD: src/secure/usr.bin/ssh-keygen/Makefile,v 1.2.2.4 2002/07/03 22:20:31 des Exp $

PROG=	ssh-keygen
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH} ${LIBCRYPTO}
LDADD=	-lssh -lcrypto

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
