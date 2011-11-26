#	$NetBSD: Makefile,v 1.1 2002/03/01 11:21:58 ad Exp $
#	$DragonFly: src/usr.bin/pkill/Makefile,v 1.3 2007/08/27 16:50:57 pavalos Exp $

PROG=	pkill

LDADD+=	-lkvm
DPADD+=	${LIBKVM}

LINKS+=	${BINDIR}/pkill ${BINDIR}/pgrep
MLINKS+=pkill.1 pgrep.1

.include <bsd.prog.mk>
