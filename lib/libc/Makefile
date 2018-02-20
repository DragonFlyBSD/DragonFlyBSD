#	@(#)Makefile	8.2 (Berkeley) 2/3/94
# $FreeBSD: src/lib/libc/Makefile,v 1.24 1999/09/29 15:18:29 marcel Exp $
#
LIB=c
SHLIB_MAJOR= 8
SHLIBDIR?= /lib
CFLAGS+= -I${.CURDIR}/include -I${.OBJDIR} -I${.CURDIR}/${MACHINE_ARCH}
CFLAGS+= -DNLS
CLEANFILES+=tags
PRECIOUSLIB=	yes

LDFLAGS+= -nodefaultlibs

#
# Include make rules that are shared with libc_rtld.
#
.include "${.CURDIR}/Makefile.inc"

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${CMAPS}

KSRCS=	bcmp.c ffs.c index.c mcount.c rindex.c strcat.c strcmp.c strcpy.c \
	strlen.c strncpy.c

libkern: libkern.gen libkern.${MACHINE_ARCH}

libkern.gen: ${KSRCS}
	cp -p ${.ALLSRC} ${DESTDIR}/sys/libkern

libkern.${MACHINE_ARCH}:: ${KMSRCS}
.if defined(KMSRCS) && !empty(KMSRCS)
	cp -p ${.ALLSRC} ${DESTDIR}/sys/libkern/${MACHINE_ARCH}
.endif

.include <bsd.lib.mk>

# Disable warnings in contributed sources.
CWARNFLAGS:=	${.IMPSRC:N*dtoa*.c:C/^.+$/${CWARNFLAGS}/:C/^$/-w/}
