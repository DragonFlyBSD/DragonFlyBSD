CONTRIBDIR=	${.CURDIR}/../../../contrib/file/src
MAGICDIR= ${CONTRIBDIR}/../magic
.PATH: ${CONTRIBDIR}

VER!=	grep -w VERSION ${.CURDIR}/config.h | awk -F\" '{print $$2}'
MAGICSHAREDIR=${SHAREDIR}/misc
MAGIC=	${MAGICSHAREDIR}/magic

LIB=	magic
SHLIB_MAJOR=	2

DPADD=  ${LIBBZ2} ${LIBLZMA} ${LIBZ}
LDADD=  -lbz2 -llzma -lz

SRCS=	apprentice.c ascmagic.c buffer.c cdf.c cdf_time.c \
	compress.c der.c encoding.c fsmagic.c funcs.c is_csv.c \
	is_json.c is_tar.c magic.c print.c readcdf.c readelf.c softmagic.c
INCS=	magic.h

# only when __EMX__ (prog env for DOS and OS/2)
#SRCS+=	apptype.c

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${.CURDIR} -I${.OBJDIR} -I${CONTRIBDIR}

MAN=	libmagic.3

MLINKS+=	libmagic.3 magic_open.3
MLINKS+=	libmagic.3 magic_close.3
MLINKS+=	libmagic.3 magic_error.3
MLINKS+=	libmagic.3 magic_errno.3
MLINKS+=	libmagic.3 magic_descriptor.3
MLINKS+=	libmagic.3 magic_file.3
MLINKS+=	libmagic.3 magic_buffer.3
MLINKS+=	libmagic.3 magic_getflags.3
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

.if !defined(NO_SHARE)
FILES=		magic magic.mgc
FILESDIR=	${MAGICSHAREDIR}
.endif

MAGFILES=	${MAGICDIR}/Header ${MAGICDIR}/Localstuff \
		${MAGICDIR}/Magdir/[a-z]*

libmagic.3: ${CONTRIBDIR}/../doc/libmagic.man
	    sed	-e s@__CSECTION__@1@g \
		-e s@__FSECTION__@5@g \
		-e s@__MAGIC__@${MAGIC}@g ${.ALLSRC} > ${.TARGET}

magic: ${MAGFILES}
	cat ${.ALLSRC:O} > ${.TARGET}

magic.mgc: magic encoding.c magic.c
	${.OBJDIR}/../mkmagic/mkmagic.nx ${.OBJDIR}/magic

CLEANFILES+=	${INCS}
.for inc in ${INCS}
${inc}: ${inc}.in
	sed -e 's,X.YY,${VER:S,",,g:S,.,,g},g' ${.ALLSRC} > ${.TARGET}
.endfor

.include <bsd.lib.mk>
