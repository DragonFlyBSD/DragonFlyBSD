# $DragonFly: src/gnu/usr.bin/binutils215/gdbserver/Attic/Makefile,v 1.1 2004/12/20 13:14:44 asmodai Exp $

.include "../Makefile.inc0"

# Not elf specific so don't install in /usr/libexec/elf
BINDIR=/usr/bin

GDBDIR=	${.CURDIR}/../../../../contrib/gdb
.PATH:	${GDBDIR}/gdb/gdbserver
.PATH:	${GDBDIR}/gdb

PROG=	gdbserver

SRCS=	remote-utils.c utils.c server.c
SRCS+=	low-fbsd.c

CFLAGS+=	-I${.CURDIR}/../gdb/${MACHINE_ARCH}
CFLAGS+=	-I${GDBDIR}/gdb
CFLAGS+=	-I${GDBDIR}/gdb/config
CFLAGS+=	-I${GDBDIR}/gdb/gdbserver
CFLAGS+=	-DNO_MMALLOC

.include <bsd.prog.mk>
