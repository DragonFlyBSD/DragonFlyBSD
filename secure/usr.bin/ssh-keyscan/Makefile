# $FreeBSD: src/secure/usr.bin/ssh-keyscan/Makefile,v 1.1.2.3 2002/07/03 22:20:32 des Exp $
# $DragonFly: src/secure/usr.bin/ssh-keyscan/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

PROG=	ssh-keyscan
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH} ${LIBCRYPTO} ${LIBZ}
LDADD=	-lssh -lcrypto -lz

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
