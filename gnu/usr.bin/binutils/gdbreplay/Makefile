# $FreeBSD: src/gnu/usr.bin/binutils/gdbreplay/Makefile,v 1.6 1999/08/27 23:34:50 peter Exp $
# $DragonFly: src/gnu/usr.bin/binutils/gdbreplay/Attic/Makefile,v 1.2 2003/06/17 04:25:44 dillon Exp $

.include "../Makefile.inc0"

# Not elf specific so don't install in /usr/libexec/elf
BINDIR=/usr/bin

GDBDIR=	${.CURDIR}/../../../../contrib/gdb
.PATH:	${GDBDIR}/gdb/gdbserver
.PATH:	${GDBDIR}/gdb

PROG=	gdbreplay
NOMAN=	yes

SRCS=	gdbreplay.c

CFLAGS+=	-I${.CURDIR}/../gdb/${MACHINE_ARCH}
CFLAGS+=	-I${GDBDIR}/gdb
CFLAGS+=	-I${GDBDIR}/gdb/config
CFLAGS+=	-I${GDBDIR}/gdb/gdbserver
CFLAGS+=	-DNO_MMALLOC

.include <bsd.prog.mk>
