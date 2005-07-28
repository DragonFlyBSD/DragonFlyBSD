# $DragonFly: src/lib/pam_module/pam_ssh/Makefile,v 1.2 2005/07/28 19:25:41 joerg Exp $

LIB=	pam_ssh
SRCS=	pam_ssh.c
WARNS?=	6
MAN=	pam_ssh.8

LDADD=	-lssh -lcrypto -lcrypt
DPADD=	${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

SSHDIR=		${.CURDIR}/../../../crypto/openssh-4
CFLAGS+=	-I${SSHDIR}

.include <bsd.lib.mk>
