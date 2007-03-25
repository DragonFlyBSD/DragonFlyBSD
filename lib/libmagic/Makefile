# $DragonFly: src/lib/libmagic/Makefile,v 1.17 2007/03/25 03:33:59 pavalos Exp $

CONTRIBDIR=	${.CURDIR}/../../contrib/file-4/src
MAGICDIR= ${CONTRIBDIR}/../magic
.PATH: ${CONTRIBDIR}

MAGIC=	/usr/share/misc/magic

LIB=	magic
SHLIB_MAJOR=	2

SRCS=	apprentice.c apptype.c ascmagic.c compress.c fsmagic.c funcs.c\
	is_tar.c magic.c print.c readelf.c softmagic.c file.h.no_obj.patch
INCS=	magic.h

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR} -I${CONTRIBDIR}

MAN=	libmagic.3

MLINKS+=	libmagic.3 magic_open.3
MLINKS+=	libmagic.3 magic_close.3
MLINKS+=	libmagic.3 magic_error.3
MLINKS+=	libmagic.3 magic_errno.3
MLINKS+=	libmagic.3 magic_file.3
MLINKS+=	libmagic.3 magic_buffer.3
MLINKS+=	libmagic.3 magic_setflags.3
MLINKS+=	libmagic.3 magic_check.3
MLINKS+=	libmagic.3 magic_compile.3
MLINKS+=	libmagic.3 magic_load.3

CLEANFILES+=	magic magic.mgc magic.mime.mgc magic.mime.lnk libmagic.3

FILES=		magic magic.mgc ${MAGICDIR}/magic.mime magic.mime.mgc
FILESDIR=	/usr/share/misc

MAGFILES=	${MAGICDIR}/Header ${MAGICDIR}/Localstuff\
		${MAGICDIR}/Magdir/[a-z]*

libmagic.3: ${CONTRIBDIR}/../doc/libmagic.man libmagic.man.patch
	  patch -o - ${.ALLSRC} | \
	    sed	-e s@__CSECTION__@1@g \
		-e s@__FSECTION__@5@g \
		-e s@__MAGIC__@${MAGIC}@g > ${.TARGET}

all: ${LIB} magic.mgc magic.mime.mgc

magic: ${MAGFILES}
	cat ${.ALLSRC} > ${.TARGET}

magic.mgc: mkmagic.nx magic
	./mkmagic.nx magic
 
magic.mime.mgc: mkmagic.nx ${MAGICDIR}/magic.mime
	./mkmagic.nx ${MAGICDIR}/magic.mime

CLEANFILES+=   mkmagic.nx
build-tools: mkmagic.nx

NXCFLAGS+=	-DHAVE_CONFIG_H -DCOMPILE_ONLY -I${.OBJDIR} -I- -I${.CURDIR} -I${CONTRIBDIR}
NXCFLAGS+=	-DBOOTSTRAPPING
mkmagic.nx: file.h apprentice.c funcs.c magic.c print.c
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC} ${NXLDLIBS} -o ${.TARGET}

.include <bsd.lib.mk>
