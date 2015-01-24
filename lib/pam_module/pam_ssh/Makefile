# $DragonFly: src/lib/pam_module/pam_ssh/Makefile,v 1.3 2008/04/07 01:20:18 pavalos Exp $

LIB=	pam_ssh
SRCS=	pam_ssh.c
MAN=	pam_ssh.8

LDADD=	-lssh -lcrypto -lcrypt
DPADD=	${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

WARNS?=	4
SSHDIR=		${.CURDIR}/../../../crypto/openssh
CFLAGS+=	-I${SSHDIR} -include ssh_namespace.h

.include <bsd.lib.mk>
