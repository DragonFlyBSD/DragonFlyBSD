# $FreeBSD: src/gnu/usr.bin/binutils/as/amd64-freebsd/Makefile,v 1.2 2003/08/14 14:43:28 ru Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/as/amd64-dragonfly/Attic/Makefile,v 1.2 2004/02/02 05:43:11 dillon Exp $

DEFAULT_ARCH=           x86_64 
EMULATIONS=             &i386elf,
DEFAULT_EMULATION=      i386elf 
TARGET_ALIAS=           x86_64-dillon-dragonfly1.0 
TARGET_CANONICAL=       x86_64-dillon-dragonfly1.0 
TARGET_CPU=             x86_64 

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
NOMAN=	true
.if ${TARGET_ARCH} != "amd64"
BINDIR=	/usr/libexec/cross/amd64-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c

.include <bsd.prog.mk>

