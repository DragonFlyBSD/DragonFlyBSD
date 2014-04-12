# $FreeBSD$

PROG=		ld-elf.so.2
SRCS=		rtld_start.S \
		reloc.c rtld.c rtld_lock.c rtld_printf.c map_object.c \
		malloc.c xmalloc.c debug.c libmap.c
MAN=		rtld.1
CSTD?=		gnu99
BINDIR=		/libexec
SYMLINKS=	${BINDIR}/${PROG} /usr/libexec/${PROG}
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

.if !defined(NO_SYMVER)
VERSION_DEF=	${.CURDIR}/../../lib/libc/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
VERSION_MAP=	Version.map
LDFLAGS+=	-Wl,--version-script=${VERSION_MAP}

rtld.c:		${VERSION_MAP}

.if exists(${.CURDIR}/${MACHINE_ARCH}/Symbol.map)
SYMBOL_MAPS+=	${.CURDIR}/${MACHINE_ARCH}/Symbol.map
.endif
.endif

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

# Since moving rtld-elf to /libexec, we need to create a symlink.
# Fixup the existing binary that's there so we can symlink over it.
beforeinstall:
.if exists(${DESTDIR}/usr/libexec/${PROG})
	chflags -h noschg ${DESTDIR}/usr/libexec/${PROG}
.endif

.PATH: ${.CURDIR}/${MACHINE_ARCH}

.include <bsd.symver.mk>
.include <bsd.prog.mk>
