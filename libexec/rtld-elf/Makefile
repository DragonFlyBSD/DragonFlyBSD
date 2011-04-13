# $FreeBSD: src/libexec/rtld-elf/Makefile,v 1.10.2.6 2002/06/22 17:03:13 jdp Exp $

PROG=		ld-elf.so.2
SRCS=		rtld_start.S rtld.c rtld_lock.c map_object.c malloc.c \
		xmalloc.c debug.c reloc.c libmap.c
MAN=		rtld.1
WARNS?=		2

# To activate LD_DEBUG functionality, define ENABLE_DEBUG
# e.g. make -DENABLE_DEBUG

CFLAGS+=	-DIN_RTLD
CFLAGS+=	-I${.CURDIR}/${MACHINE_ARCH} -I${.CURDIR} -D__thread=
LDFLAGS+=	-nostdlib -e .rtld_start -Wl,--no-undefined
INSTALLFLAGS=	-C -b
.ifndef NOFSCHG
INSTALLFLAGS+=	-fschg
.endif
MLINKS=		rtld.1 ld-elf.so.1.1
MLINKS+=	rtld.1 ld-elf.so.2.1
MLINKS+=	rtld.1 _rtld_functrace.3

.if exists(${.CURDIR}/${MACHINE_ARCH}/Makefile.inc)
.include "${.CURDIR}/${MACHINE_ARCH}/Makefile.inc"
.endif

.ifdef LDSCRIPT
OBJS+=		dyn_hack.so
LDFLAGS+=	-Wl,-T,${LDSCRIPT} -Wl,-E -Wl,-Bstatic
DPADD=		${.OBJDIR}/../../lib/libc_rtld/libc_rtld.a
LDADD=		-L${.OBJDIR}/../../lib/libc_rtld -lc_rtld
.else
CFLAGS+=	-fpic -DPIC
LDFLAGS+=	-shared -Wl,-Bsymbolic
DPADD=		${.OBJDIR}/../../lib/libc_rtld/libc_rtld_pic.a
LDADD=		-L${.OBJDIR}/../../lib/libc_rtld -lc_rtld_pic
.endif

.ifdef ENABLE_DEBUG
CFLAGS+=	-DDEBUG
.endif

dyn_hack.so:
	${CC} -shared -nostdlib -o dyn_hack.so -x c /dev/null

.PATH: ${.CURDIR}/${MACHINE_ARCH}

.include <bsd.prog.mk>
