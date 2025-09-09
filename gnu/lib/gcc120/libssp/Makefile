CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.include "../Makefile.inc"

.PATH: ${GCCDIR}/libssp

CFLAGS+=	-DHAVE_CONFIG_H
VERSION_MAP=	${GCCDIR}/libssp/ssp.map

LIB=		ssp
SHLIB_MAJOR=	0

# From libssp Makefile
libssp_la_SOURCES = \
	ssp.c gets-chk.c memcpy-chk.c memmove-chk.c mempcpy-chk.c \
	memset-chk.c snprintf-chk.c sprintf-chk.c stpcpy-chk.c \
	strcat-chk.c strcpy-chk.c strncat-chk.c strncpy-chk.c \
	vsnprintf-chk.c vsprintf-chk.c

SRCS=	${libssp_la_SOURCES}

# generated
SRCS+=	ssp.h

ssp.h: ${GCCDIR}/libssp/ssp/ssp.h.in
	sed -e 's/@ssp_have_usable_vsnprintf@/define/' \
	    < ${.ALLSRC:Nsspdir} > ${.TARGET}

INCS+=		ssp.h
INCSDIR=	/usr/libdata/gcc${GCCSHORTVER}/ssp

CLEANFILES+= ssp.h

.include <bsd.lib.mk>
