# $DragonFly: src/gnu/usr.bin/binutils217/as/i386-dragonfly/Makefile,v 1.1 2007/04/13 12:24:32 corecode Exp $

DEFAULT_ARCH=		i386
EMULATIONS=		&i386elf,
DEFAULT_EMULATION=	i386elf
TARGET_ALIAS=		i386-just-dragonfly1.9
TARGET_CANONICAL=	i386-just-dragonfly1.9
TARGET_CPU=		i386

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
.if ${TARGET_ARCH} != "i386"
BINDIR=	/usr/libexec/cross/i386-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c
CLEANFILES=	tc-i386-local.c

.include <bsd.prog.mk>

