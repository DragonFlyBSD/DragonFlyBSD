# $DragonFly: src/usr.bin/file/Makefile,v 1.6 2005/07/04 17:19:34 swildner Exp $

SRCDIR=	${.CURDIR}/../../contrib/file-4/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

PROG=	file
MAN=	file.1 magic.5

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR}

LDADD+=	-lmagic -lz
DPADD+=	${LIBMAGIC} ${LIBZ}

.include <bsd.prog.mk>
