# $FreeBSD: src/secure/usr.bin/ssh-agent/Makefile,v 1.2.2.3 2002/07/03 22:20:31 des Exp $
# $DragonFly: src/secure/usr.bin/ssh-agent/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

PROG=	ssh-agent
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH} ${LIBCRYPTO}
LDADD=	-lssh -lcrypto

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
