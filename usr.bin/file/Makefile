# $DragonFly: src/usr.bin/file/Makefile,v 1.5 2005/06/01 14:34:46 joerg Exp $

SRCDIR=	${.CURDIR}/../../contrib/file-4/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

PROG=	file

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR}

LDADD+=	-lmagic -lz
DPADD+=	${LIBMAGIC} ${LIBZ}

.include <bsd.prog.mk>
