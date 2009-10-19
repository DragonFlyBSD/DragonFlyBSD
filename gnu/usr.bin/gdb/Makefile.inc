BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/gdb-7

GDBLIBS+=	${.OBJDIR}/../libopcodes/libopcodes.a
GDBLIBS+=	${.OBJDIR}/../libgdb/libgdb.a
GDBLIBS+=	${.OBJDIR}/../libbfd/libbfd.a
GDBLIBS+=	${.OBJDIR}/../libiberty/libiberty.a

.include "../Makefile.inc"
