# $FreeBSD: src/secure/usr.bin/scp/Makefile,v 1.2.2.3 2002/07/03 22:20:29 des Exp $
# $DragonFly: src/secure/usr.bin/scp/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

PROG=	scp
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH}
LDADD=	-lssh

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
