# $FreeBSD: src/usr.bin/minigzip/Makefile,v 1.3 1999/08/28 01:04:00 peter Exp $
# $DragonFly: src/usr.bin/minigzip/Attic/Makefile,v 1.4 2005/01/16 21:14:51 cpressey Exp $

PROG=	minigzip
WARNS?=	6
LDADD+=	-lz
DPADD+=	${LIBZ}

.include "../../lib/libz/Makefile.minigzip"

.include <bsd.prog.mk>
