# $DragonFly: src/usr.bin/file/Makefile,v 1.7 2006/11/11 21:52:12 pavalos Exp $

SRCDIR=	${.CURDIR}/../../contrib/file-4/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

PROG=	file
MAN=	file.1 magic.5

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR}/../../lib/libmagic

LDADD+=	-lmagic -lz
DPADD+=	${LIBMAGIC} ${LIBZ}

.include <bsd.prog.mk>
