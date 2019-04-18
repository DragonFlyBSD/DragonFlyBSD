# $FreeBSD: src/secure/libexec/ssh-keysign/Makefile,v 1.1.2.2 2003/02/03 17:31:12 des Exp $

PROG=	ssh-keysign
SRCS=	ssh-keysign.c
SRCS+=	readconf.c uidswap.c
MAN=	ssh-keysign.8
.if defined(ENABLE_SUID_SSH)
BINMODE=4511
.endif

WARNS=	3

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
