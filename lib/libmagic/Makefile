CONTRIBDIR=	${.CURDIR}/../../contrib/file/src
MAGICDIR= ${CONTRIBDIR}/../magic
.PATH: ${CONTRIBDIR}

MAGIC=	${FILESDIR}/magic

LIB=	magic
SHLIB_MAJOR=	2

DPADD=	${LIBZ}
LDADD=	-lz

SRCS=	apprentice.c apptype.c ascmagic.c cdf.c cdf_time.c \
	compress.c encoding.c fsmagic.c funcs.c \
	is_tar.c magic.c print.c readcdf.c readelf.c softmagic.c
INCS=	magic.h

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR} -I${CONTRIBDIR}

MAN=	libmagic.3

MLINKS+=	libmagic.3 magic_open.3
MLINKS+=	libmagic.3 magic_close.3
MLINKS+=	libmagic.3 magic_error.3
MLINKS+=	libmagic.3 magic_errno.3
MLINKS+=	libmagic.3 magic_descriptor.3
MLINKS+=	libmagic.3 magic_file.3
MLINKS+=	libmagic.3 magic_buffer.3
MLINKS+=	libmagic.3 magic_setflags.3
MLINKS+=	libmagic.3 magic_check.3
MLINKS+=	libmagic.3 magic_compile.3
MLINKS+=	libmagic.3 magic_list.3
MLINKS+=	libmagic.3 magic_load.3
MLINKS+=	libmagic.3 magic_load_buffers.3
MLINKS+=	libmagic.3 magic_setparam.3
MLINKS+=	libmagic.3 magic_getparam.3
MLINKS+=	libmagic.3 magic_version.3

CLEANFILES+=	magic magic.mgc libmagic.3

FILES=		magic magic.mgc
FILESDIR=	${SHAREDIR}/misc

MAGFILES=	${MAGICDIR}/Header ${MAGICDIR}/Localstuff\
		${MAGICDIR}/Magdir/[a-z]*

libmagic.3: ${CONTRIBDIR}/../doc/libmagic.man
	    sed	-e s@__CSECTION__@1@g \
		-e s@__FSECTION__@5@g \
		-e s@__MAGIC__@${MAGIC}@g ${.ALLSRC} > ${.TARGET}

magic: ${MAGFILES}
	cat ${.ALLSRC} > ${.TARGET}

magic.mgc: mkmagic.nx magic
	./mkmagic.nx magic

CLEANFILES+=   mkmagic.nx
build-tools: mkmagic.nx

NXCFLAGS+=	-w -DCOMPILE_ONLY
mkmagic.nx: apprentice.c encoding.c cdf_time.c funcs.c magic.c print.c
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${.ALLSRC} ${NXLDLIBS} -o ${.TARGET}

.include <bsd.lib.mk>
