# $FreeBSD: src/gnu/usr.bin/binutils/as/i386-freebsd/Makefile,v 1.7.2.3 2002/09/01 23:39:13 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/as/i386-dragonfly/Attic/Makefile,v 1.1 2004/02/01 08:53:03 dillon Exp $

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
NOMAN=	true
.if ${TARGET_ARCH} != "i386"
BINDIR=	/usr/libexec/cross/i386-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c

.include <bsd.prog.mk>
