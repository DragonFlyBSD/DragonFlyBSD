BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/gdb-7

GDBLIBS+=	${.OBJDIR}/../libreadline/libreadline.a
GDBLIBS+=	${.OBJDIR}/../libopcodes/libopcodes.a
GDBLIBS+=	${.OBJDIR}/../libbfd/libbfd.a
GDBLIBS+=	${.OBJDIR}/../libiberty/libiberty.a
GDBLIBS+=	${.OBJDIR}/../libdecnumber/libdecnumber.a
GDBLIBS+=	${.OBJDIR}/../libgdb/libgdb.a

.include "../Makefile.inc"
