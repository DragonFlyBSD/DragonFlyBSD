# $DragonFly: src/usr.bin/file/Makefile,v 1.4 2004/12/18 17:39:17 asmodai Exp $

FILEVER= 4.12
SRCDIR=	${.CURDIR}/../../contrib/file-${FILEVER}/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

PROG=	file

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR}

LDADD+=	-lmagic -lz
DPADD+=	${LIBMAGIC} ${LIBZ}

.include <bsd.prog.mk>
