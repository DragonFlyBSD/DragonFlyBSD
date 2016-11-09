# $FreeBSD: src/usr.bin/top/Makefile,v 1.5.6.2 2002/08/11 17:09:54 dwmalone Exp $
PROG=	top

TOPDIR=	${.CURDIR}/../../contrib/top
.PATH:	${TOPDIR}

CFLAGS+= -DHAVE_GETOPT -DHAVE_STRERROR -I${.CURDIR} -I${.OBJDIR} \
		 -I${TOPDIR} -I. -DORDER

.if ${MACHINE_ARCH} == "i386"
CFLAGS+=-D_ENABLEX32
.endif
.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=-D_ENABLEX64
.endif

SRCS=	commands.c getopt.c screen.c username.c version.c \
	color.c display.c hash.c m_dragonfly.c top.c utils.c

DPADD=  ${LIBNCURSES} ${LIBM} ${LIBKVM} ${LIBKINFO}
LDADD=  -lprivate_ncurses -lm -lkvm -lkinfo
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
BINGRP= kmem
BINMODE=2555

WARNS?= 1

.include <bsd.prog.mk>
