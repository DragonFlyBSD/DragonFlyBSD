# $DragonFly: src/lib/libmagic/Makefile,v 1.5 2004/12/18 18:14:20 asmodai Exp $

FILEVER= 4.12
MAGICDIR= ${.CURDIR}/../../contrib/file-${FILEVER}/magic
SRCDIR=	${.CURDIR}/../../contrib/file-${FILEVER}/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

LIB=	magic
SHLIB_MAJOR=	1

SRCS=	apprentice.c apptype.c ascmagic.c compress.c fsmagic.c funcs.c\
	is_tar.c magic.c print.c readelf.c softmagic.c
INCS=	magic.h

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR} -I${SRCDIR}



CLEANFILES+=	magic magic.mgc magic.mime.mgc magic.mime.lnk

FILES=		magic magic.mgc ${MAGICDIR}/magic.mime magic.mime.mgc
FILESDIR=	/usr/share/misc

MAGFILES=	${MAGICDIR}/Header ${MAGICDIR}/Localstuff\
		${MAGICDIR}/Magdir/[a-z]*

all: ${LIB} magic.mgc magic.mime.mgc

magic: ${MAGFILES}
	cat ${.ALLSRC} > ${.TARGET}

magic.mgc: mkmagic magic
	./mkmagic magic
 
magic.mime.mgc: mkmagic ${MAGICDIR}/magic.mime
	./mkmagic ${MAGICDIR}/magic.mime

CLEANFILES+=   mkmagic
build-tools: mkmagic
mkmagic: apprentice.c funcs.c magic.c print.c
	${CC} -DHAVE_CONFIG_H -DCOMPILE_ONLY \
		-I${.CURDIR} -I${SRCDIR} -o ${.TARGET} ${.ALLSRC}

.include <bsd.lib.mk>
