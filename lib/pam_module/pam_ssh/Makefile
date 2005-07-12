# $DragonFly: src/lib/pam_module/pam_ssh/Makefile,v 1.1 2005/07/12 23:26:49 joerg Exp $

LIB=	pam_ssh
SRCS=	pam_ssh.c
WARNS?=	6
MAN=	pam_ssh.8

LDADD=	-lssh -lcrypto -lcrypt

SSHDIR=		${.CURDIR}/../../../crypto/openssh-4
CFLAGS+=	-I${SSHDIR}

.include <bsd.lib.mk>
