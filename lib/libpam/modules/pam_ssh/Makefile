# PAM module for SSH
# $FreeBSD: src/lib/libpam/modules/pam_ssh/Makefile,v 1.2.2.5 2002/07/03 21:41:30 des Exp $
# $DragonFly: src/lib/libpam/modules/pam_ssh/Attic/Makefile,v 1.3 2004/07/31 19:03:00 geekgod Exp $

SSHSRC=	${.CURDIR}/../../../../crypto/openssh-3.8.1p1

LIB=		pam_ssh
SRCS=		pam_ssh.c
CFLAGS+=	-I${SSHSRC}
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT} ${LIBUTIL}
LDADD=		-lssh -lcrypto -lcrypt -lutil
MAN=		pam_ssh.8

.include <bsd.lib.mk>
