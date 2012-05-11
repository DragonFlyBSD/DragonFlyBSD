#PROG=	gdbtui
#
#SRCS=	tui-main.c
#
#CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
#CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/common
#CFLAGS+= -I${BASEDIR}/gdb/config
#CFLAGS+= -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd
#
#DPADD=	${GDBLIBS}
#DPADD+=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM} ${LIBZ}
#LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty
#LDADD+=	-L../libdecnumber -ldecnumber
#LDADD+=	-ledit -ltermcap -lm -lz
#
#.PATH:	${BASEDIR}/gdb ${BASEDIR}/gdb/tui

SCRIPTS=	gdbtui
NOMAN=

.include <bsd.prog.mk>
