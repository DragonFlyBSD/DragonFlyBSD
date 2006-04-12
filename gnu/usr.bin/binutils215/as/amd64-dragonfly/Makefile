# $DragonFly: src/gnu/usr.bin/binutils215/as/amd64-dragonfly/Attic/Makefile,v 1.3 2006/04/12 19:39:55 swildner Exp $

DEFAULT_ARCH=           x86_64 
EMULATIONS=             &i386elf,
DEFAULT_EMULATION=      i386elf 
TARGET_ALIAS=           x86_64-just-dragonfly1.1 
TARGET_CANONICAL=       x86_64-just-dragonfly1.1 
TARGET_CPU=             x86_64 

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
.if ${TARGET_ARCH} != "amd64"
BINDIR=	/usr/libexec/cross/amd64-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c
CLEANFILES=	tc-i386-local.c

.include <bsd.prog.mk>

