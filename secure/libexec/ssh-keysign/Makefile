# $FreeBSD: src/secure/libexec/ssh-keysign/Makefile,v 1.1.2.2 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/libexec/ssh-keysign/Makefile,v 1.5 2008/04/07 01:20:18 pavalos Exp $

PROG=	ssh-keysign
SRCS=	ssh-keysign.c readconf.c roaming_dummy.c
SRCS+=	fmt_scaled.c
MAN=	ssh-keysign.8
.if defined(ENABLE_SUID_SSH)
BINMODE=4511
.endif

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
