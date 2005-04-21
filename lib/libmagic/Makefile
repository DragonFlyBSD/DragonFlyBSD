# $DragonFly: src/lib/libmagic/Makefile,v 1.7 2005/04/21 13:42:34 joerg Exp $

FILEVER= 4.12
MAGICDIR= ${.CURDIR}/../../contrib/file-${FILEVER}/magic
SRCDIR=	${.CURDIR}/../../contrib/file-${FILEVER}/src
.PATH: ${SRCDIR}

MAGIC=	/usr/share/misc/magic

LIB=	magic
SHLIB_MAJOR=	2

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

magic.mgc: mkmagic.nx magic
	./mkmagic.nx magic
 
magic.mime.mgc: mkmagic.nx ${MAGICDIR}/magic.mime
	./mkmagic.nx ${MAGICDIR}/magic.mime

CLEANFILES+=   mkmagic.nx
build-tools: mkmagic.nx

NXCFLAGS+=	-DHAVE_CONFIG_H -DCOMPILE_ONLY -I${.CURDIR} -I${SRCDIR}
mkmagic.nx: apprentice.c funcs.c magic.c print.c
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC} ${NXLDLIBS} -o ${.TARGET}

.include <bsd.lib.mk>
