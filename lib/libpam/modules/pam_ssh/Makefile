# PAM module for SSH
# $FreeBSD: src/lib/libpam/modules/pam_ssh/Makefile,v 1.2.2.5 2002/07/03 21:41:30 des Exp $
# $DragonFly: src/lib/libpam/modules/pam_ssh/Attic/Makefile,v 1.4 2004/08/30 21:59:58 geekgod Exp $

SSHSRC=	${.CURDIR}/../../../../crypto/openssh-3.9p1

LIB=		pam_ssh
SRCS=		pam_ssh.c
CFLAGS+=	-I${SSHSRC} -D__dead=__dead2
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT} ${LIBUTIL}
LDADD=		-lssh -lcrypto -lcrypt -lutil
MAN=		pam_ssh.8

.include <bsd.lib.mk>
