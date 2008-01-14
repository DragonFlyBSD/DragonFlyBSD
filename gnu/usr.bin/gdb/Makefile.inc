# $DragonFly: src/gnu/usr.bin/gdb/Makefile.inc,v 1.4 2008/01/14 21:36:38 corecode Exp $

BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/gdb-6

GDBLIBS+=	${.OBJDIR}/../libopcodes/libopcodes.a
GDBLIBS+=	${.OBJDIR}/../libgdb/libgdb.a
GDBLIBS+=	${.OBJDIR}/../libbfd/libbfd.a
GDBLIBS+=	${.OBJDIR}/../libiberty/libiberty.a

.include "../Makefile.inc"
