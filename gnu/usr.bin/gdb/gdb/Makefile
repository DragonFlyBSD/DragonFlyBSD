# $DragonFly: src/gnu/usr.bin/gdb/gdb/Makefile,v 1.11 2007/10/27 22:36:47 pavalos Exp $

PROG=	gdb
SRCS=	gdb.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CLFAGS+= -I${BASEDIR}

DPADD=	${GDBLIBS}
DPADD+=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty
LDADD+=	-ledit -lkvm -ltermcap -lm

SUBDIR=	doc

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
