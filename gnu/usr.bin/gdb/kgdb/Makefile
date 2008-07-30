# $DragonFly: src/gnu/usr.bin/gdb/kgdb/Makefile,v 1.4 2008/07/30 01:48:14 corecode Exp $

PROG=	kgdb
SRCS=	kgdb.c kthr.c trgt.c trgt_${MACHINE_ARCH}.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd

DPADD=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	${GDBLIBS}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes
LDADD+= -L../libiberty -liberty
LDADD+=	-ledit -lkvm -ltermcap -lm

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
