# $FreeBSD: src/secure/usr.bin/ssh-keyscan/Makefile,v 1.1.2.3 2002/07/03 22:20:32 des Exp $
# $DragonFly: src/secure/usr.bin/ssh-keyscan/Makefile,v 1.3 2004/07/31 19:03:02 geekgod Exp $

PROG=	ssh-keyscan

SRCS=	ssh-keyscan.c ${.CURDIR}/../../usr.sbin/sshd/myproposal.h.patch

DPADD+=	${LIBZ}
LDADD+=	-lz

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
