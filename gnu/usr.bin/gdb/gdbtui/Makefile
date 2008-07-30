# $DragonFly: src/gnu/usr.bin/gdb/gdbtui/Makefile,v 1.2 2008/07/30 01:48:14 corecode Exp $

PROG=	gdbtui
SRCS=	tui-main.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd

DPADD=	${GDBLIBS}
DPADD+=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty
LDADD+=	-ledit -lkvm -ltermcap -lm

NOMAN=	#

#SUBDIR=	doc

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb ${BASEDIR}/gdb/tui
