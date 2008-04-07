# $DragonFly: src/lib/pam_module/pam_ssh/Makefile,v 1.3 2008/04/07 01:20:18 pavalos Exp $

LIB=	pam_ssh
SRCS=	pam_ssh.c
WARNS?=	6
MAN=	pam_ssh.8

LDADD=	-lssh -lcrypto -lcrypt
DPADD=	${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

SSHDIR=		${.CURDIR}/../../../crypto/openssh-5
CFLAGS+=	-I${SSHDIR}

.include <bsd.lib.mk>
