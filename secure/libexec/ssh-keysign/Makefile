# $FreeBSD: src/secure/libexec/ssh-keysign/Makefile,v 1.1.2.2 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/libexec/ssh-keysign/Makefile,v 1.4 2004/08/30 21:59:58 geekgod Exp $

PROG=	ssh-keysign
SRCS=	ssh-keysign.c readconf.c uidswap.c
MAN=	ssh-keysign.8
.if defined(ENABLE_SUID_SSH)
BINMODE=4511
.endif

SRCS+=	${.CURDIR}/../../usr.bin/ssh/readconf.c.patch

DPADD+=	${LIBZ}
LDADD+=	-lz

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
