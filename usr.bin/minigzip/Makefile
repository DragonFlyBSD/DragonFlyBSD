# $FreeBSD: src/usr.bin/minigzip/Makefile,v 1.3 1999/08/28 01:04:00 peter Exp $
# $DragonFly: src/usr.bin/minigzip/Attic/Makefile,v 1.3 2004/10/23 15:58:18 joerg Exp $

PROG=	minigzip
LDADD+=	-lz
DPADD+=	${LIBZ}

.include "../../lib/libz/Makefile.minigzip"

.include <bsd.prog.mk>
