CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.include "../Makefile.inc"

.PATH: ${GCCDIR}/libssp

CFLAGS+=	-DHAVE_CONFIG_H
LDFLAGS+=	-Wl,--version-script=${GCCDIR}/libssp/ssp.map

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

sspdir:
	mkdir -p ssp

ssp.h: sspdir ${GCCDIR}/libssp/ssp/ssp.h.in
	sed -e 's/@ssp_have_usable_vsnprintf@/define/' \
	    < ${.ALLSRC:Nsspdir} > ssp/${.TARGET}

CLEANDIRS+=	ssp

.include <bsd.lib.mk>
