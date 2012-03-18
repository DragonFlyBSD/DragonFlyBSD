# $FreeBSD$

PROG=		ld-elf.so.2
SRCS=		rtld_start.S \
		reloc.c rtld.c rtld_lock.c rtld_printf.c map_object.c \
		malloc.c xmalloc.c debug.c libmap.c
MAN=		rtld.1
CSTD?=		gnu99
WARNS?=		2

# To activate LD_DEBUG functionality, define ENABLE_DEBUG
# e.g. make -DENABLE_DEBUG

CFLAGS+=	-DIN_RTLD
CFLAGS+=	-I${.CURDIR}/${MACHINE_ARCH} -I${.CURDIR} -D__thread=
CFLAGS+=	-I${.CURDIR}/../../lib/csu/common
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

CFLAGS+=	-fpic -DPIC
LDFLAGS+=	-shared -Wl,-Bsymbolic
DPADD=		${.OBJDIR}/../../lib/libc_rtld/libc_rtld_pic.a
LDADD=		-L${.OBJDIR}/../../lib/libc_rtld -lc_rtld_pic

.ifdef ENABLE_DEBUG
CFLAGS+=	-DDEBUG
.endif

.PATH: ${.CURDIR}/${MACHINE_ARCH}

.include <bsd.prog.mk>
