# $FreeBSD: src/secure/usr.bin/scp/Makefile,v 1.2.2.3 2002/07/03 22:20:29 des Exp $

PROG=	scp
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH}
LDADD=	-lssh

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
