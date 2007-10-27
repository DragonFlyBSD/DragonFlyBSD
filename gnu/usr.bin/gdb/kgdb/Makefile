# $DragonFly: src/gnu/usr.bin/gdb/kgdb/Makefile,v 1.3 2007/10/27 22:36:47 pavalos Exp $

PROG=	kgdb
SRCS=	kgdb.c kthr.c trgt.c trgt_${MACHINE_ARCH}.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CLFAGS+= -I${BASEDIR}

DPADD=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	${GDBLIBS}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes
LDADD+= -L../libiberty -liberty
LDADD+=	-ledit -lkvm -ltermcap -lm

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
