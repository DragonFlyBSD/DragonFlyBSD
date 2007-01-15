# $DragonFly: src/gnu/usr.bin/gdb/kgdb/Makefile,v 1.2 2007/01/15 17:51:23 corecode Exp $

PROG=	kgdb
SRCS=	kgdb.c kthr.c trgt.c trgt_${MACHINE_ARCH}.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CLFAGS+= -I${BASEDIR}

DPADD=	${LIBREADLINE} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	${GDBLIBS}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes
LDADD+= -L../libiberty -liberty
LDADD+=	-lreadline -lkvm -ltermcap -lm

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
