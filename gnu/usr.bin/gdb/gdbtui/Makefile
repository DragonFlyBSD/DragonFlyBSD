PROG=	gdbtui
SRCS=	tui-main.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd

DPADD=	${GDBLIBS}
DPADD+=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM} ${LIBZ}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty
LDADD+=	-L../libdecnumber -ldecnumber
LDADD+=	-ledit -ltermcap -lm -lz

NOMAN=	#

#SUBDIR=	doc

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb ${BASEDIR}/gdb/tui
