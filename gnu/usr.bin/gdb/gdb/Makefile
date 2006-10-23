# $DragonFly: src/gnu/usr.bin/gdb/gdb/Makefile,v 1.9 2006/10/23 09:14:47 corecode Exp $

PROG=	gdb
SRCS=	gdb.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CLFAGS+= -I${BASEDIR}

DPADD=	${GDBLIBS}
DPADD+=	${LIBREADLINE} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty
LDADD+=	-lreadline -lkvm -ltermcap -lm

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
