# $DragonFly: src/lib/libmagic/Makefile,v 1.1 2004/12/18 17:18:43 asmodai Exp $

FILEVER=	4.12
SOURCEDIR=	${.CURDIR}/../../contrib/file-${FILEVER}/src
.PATH: ${SOURCEDIR}

MAGIC=	/usr/share/misc/magic

LIB=	magic
SHLIB_MAJOR=	1

SRCS=	apprentice.c apptype.c ascmagic.c compress.c fsmagic.c funcs.c\
	is_tar.c magic.c print.c readelf.c softmagic.c
INCS=	magic.h

CFLAGS+=	-DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+=	-I${.CURDIR} -I${SOURCEDIR}

.include <bsd.lib.mk>
