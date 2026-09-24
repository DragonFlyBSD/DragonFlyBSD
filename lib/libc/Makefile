#	@(#)Makefile	8.2 (Berkeley) 2/3/94
# $FreeBSD: src/lib/libc/Makefile,v 1.24 1999/09/29 15:18:29 marcel Exp $

LIB=c
SHLIB_MAJOR= 8
SHLIBDIR?= /lib

PRECIOUSLIB=	yes
WARNS=	2

CFLAGS+=	-I${.CURDIR}/include -I${.OBJDIR} -I${.CURDIR}/${MACHINE_ARCH}
CFLAGS+=	-DNLS
CFLAGS+=	-D__LIBC

LDFLAGS+=	-nodefaultlibs

# for now do not let the use of -flto (check -plugin-opt=-pass-through=-lc !)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

.include "${MACHINE_ARCH}/Makefile.inc"
.include "citrus/Makefile.inc"
.include "compat-43/Makefile.inc"
.include "db/Makefile.inc"
.include "gdtoa/Makefile.inc"
.include "gen/Makefile.inc"
.include "gmon/Makefile.inc"
.include "iconv/Makefile.inc"
.include "inet/Makefile.inc"
.include "isc/Makefile.inc"
.include "locale/Makefile.inc"
.include "nameser/Makefile.inc"
.include "net/Makefile.inc"
.include "nls/Makefile.inc"
.include "resolv/Makefile.inc"
.include "rpc/Makefile.inc"
.include "secure/Makefile.inc"
.include "stdio/Makefile.inc"
.include "stdlib/Makefile.inc"
.include "stdtime/Makefile.inc"
.include "string/Makefile.inc"
.include "sys/Makefile.inc"
.include "sysvipc/Makefile.inc"
.include "thread/Makefile.inc"
.include "tre-regex/Makefile.inc"
.include "upmap/Makefile.inc"
.include "uuid/Makefile.inc"
.include "xdr/Makefile.inc"

.if !defined(NO_YP_LIBC)
CFLAGS+=	-DYP
.include "yp/Makefile.inc"
.endif
.if defined(WANT_HESIOD)
CFLAGS+=	-DHESIOD
.endif
.if !defined(NO_NS_CACHING)
CFLAGS+=	-DNS_CACHING
.endif

# If there are no machine dependent sources, append all the
# machine-independent sources:
.if empty(MDSRCS)
SRCS+=	${MISRCS}
.else
# Append machine-dependent sources, then append machine-independent sources
# for which there is no machine-dependent variant.
SRCS+=	${MDSRCS}
.for _src in ${MISRCS}
.if ${MDSRCS:R:M${_src:R}} == ""
SRCS+=	${_src}
.endif
.endfor
.endif

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${CMAPS}

KSRCS=	bcmp.c ffs.c index.c rindex.c strcat.c strcmp.c strcpy.c \
	strlen.c strncpy.c

libkern: libkern.gen libkern.${MACHINE_ARCH}

libkern.gen: ${KSRCS}
	cp -p ${.ALLSRC} ${DESTDIR}/sys/libkern

libkern.${MACHINE_ARCH}:: ${KMSRCS}
.if defined(KMSRCS) && !empty(KMSRCS)
	cp -p ${.ALLSRC} ${DESTDIR}/sys/libkern/${MACHINE_ARCH}
.endif

.include <bsd.lib.mk>
