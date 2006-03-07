# $DragonFly: src/gnu/usr.bin/gdb/Makefile.inc,v 1.3 2006/03/07 15:48:11 corecode Exp $

BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/gdb-6.2.1

GDBLIBS+=	${.OBJDIR}/../libopcodes/libopcodes.a
GDBLIBS+=	${.OBJDIR}/../libgdb/libgdb.a
GDBLIBS+=	${.OBJDIR}/../libbfd/libbfd.a
GDBLIBS+=	${.OBJDIR}/../libiberty/libiberty.a

.include "../Makefile.inc"
