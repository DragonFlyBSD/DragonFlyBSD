# $DragonFly: src/lib/libmagic/Makefile,v 1.19 2007/05/28 18:42:39 pavalos Exp $

CONTRIBDIR=	${.CURDIR}/../../contrib/file/src
MAGICDIR= ${CONTRIBDIR}/../magic
.PATH: ${CONTRIBDIR}

MAGIC=	/usr/share/misc/magic

LIB=	magic
SHLIB_MAJOR=	2

SRCS=	apprentice.c apptype.c ascmagic.c cdf.c cdf_time.c \
	compress.c encoding.c fsmagic.c funcs.c \
	is_tar.c magic.c print.c readcdf.c readelf.c softmagic.c
INCS=	magic.h

LIBMAGIC_CDEFS = -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= ${LIBMAGIC_CDEFS}
CFLAGS+= -I${.CURDIR} -I${CONTRIBDIR}

MAN=	libmagic.3

MLINKS+=	libmagic.3 magic_open.3
MLINKS+=	libmagic.3 magic_close.3
MLINKS+=	libmagic.3 magic_error.3
MLINKS+=	libmagic.3 magic_descriptor.3
MLINKS+=	libmagic.3 magic_buffer.3
MLINKS+=	libmagic.3 magic_setflags.3
MLINKS+=	libmagic.3 magic_check.3
MLINKS+=	libmagic.3 magic_compile.3
MLINKS+=	libmagic.3 magic_load.3

CLEANFILES+=	magic magic.mgc libmagic.3

FILES=		magic magic.mgc
FILESDIR=	/usr/share/misc

MAGFILES=	${MAGICDIR}/Header ${MAGICDIR}/Localstuff\
		${MAGICDIR}/Magdir/[a-z]*

libmagic.3: ${CONTRIBDIR}/../doc/libmagic.man
	    sed	-e s@__CSECTION__@1@g \
		-e s@__FSECTION__@5@g \
		-e s@__MAGIC__@${MAGIC}@g ${.ALLSRC} > ${.TARGET}

all: ${LIB} magic.mgc

magic: ${MAGFILES}
	cat ${.ALLSRC} > ${.TARGET}

magic.mgc: mkmagic.nx magic
	./mkmagic.nx magic

CLEANFILES+=   mkmagic.nx
build-tools: mkmagic.nx

NXCFLAGS=	${LIBMAGIC_CDEFS} -DCOMPILE_ONLY -I${.OBJDIR} -I- -I${.CURDIR} -I${CONTRIBDIR}
NXCFLAGS+=	-DBOOTSTRAPPING
mkmagic.nx: file.h apprentice.c funcs.c magic.c print.c
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC} ${NXLDLIBS} -o ${.TARGET}

.include <bsd.lib.mk>
