# $FreeBSD: src/secure/usr.bin/sftp/Makefile,v 1.1.2.3 2002/07/03 22:20:30 des Exp $
# $DragonFly: src/secure/usr.bin/sftp/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

PROG=	sftp
SRCS=   sftp.c sftp-client.c sftp-int.c sftp-common.c sftp-glob.c
CFLAGS+=-I${SSHDIR}

DPADD=	${LIBSSH} ${LIBCRYPTO}
LDADD=	-lssh -lcrypto

.include <bsd.prog.mk>

.PATH:	${SSHDIR}
