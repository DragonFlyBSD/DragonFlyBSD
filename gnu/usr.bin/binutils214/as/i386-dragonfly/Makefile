# $FreeBSD: src/gnu/usr.bin/binutils/as/i386-freebsd/Makefile,v 1.7.2.3 2002/09/01 23:39:13 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/as/i386-dragonfly/Attic/Makefile,v 1.2 2004/02/02 05:43:11 dillon Exp $

DEFAULT_ARCH=		i386
EMULATIONS=		&i386elf,
DEFAULT_EMULATION=	i386elf
TARGET_ALIAS=		i386-dillon-dragonfly1.0
TARGET_CANONICAL=	i386-dillon-dragonfly1.0
TARGET_CPU=		i386

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
NOMAN=	true
.if ${TARGET_ARCH} != "i386"
BINDIR=	/usr/libexec/cross/i386-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c

.include <bsd.prog.mk>

