# $DragonFly: src/gnu/usr.bin/binutils215/as/i386-dragonfly/Attic/Makefile,v 1.1 2004/12/20 13:14:44 asmodai Exp $

DEFAULT_ARCH=		i386
EMULATIONS=		&i386elf,
DEFAULT_EMULATION=	i386elf
TARGET_ALIAS=		i386-unknown-dragonfly1.1
TARGET_CANONICAL=	i386-unknown-dragonfly1.1
TARGET_CPU=		i386

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
NOMAN=	true
.if ${TARGET_ARCH} != "i386"
BINDIR=	/usr/libexec/cross/i386-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c
CLEANFILES=	tc-i386-local.c

.include <bsd.prog.mk>

