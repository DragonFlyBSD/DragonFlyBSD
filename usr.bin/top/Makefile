# $FreeBSD: src/usr.bin/top/Makefile,v 1.5.6.2 2002/08/11 17:09:54 dwmalone Exp $
PROG=	top

CFLAGS+= -DORDER -I${.CURDIR}

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=-D_ENABLEX64
.endif

SRCS=	commands.c screen.c username.c version.c \
	color.c display.c hash.c m_dragonfly.c top.c utils.c

DPADD=  ${LIBNCURSES} ${LIBM} ${LIBKVM} ${LIBKINFO}
LDADD=  -lprivate_ncurses -lm -lkvm -lkinfo

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

BINGRP= kmem
BINMODE=2555

WARNS?= 2

.include <bsd.prog.mk>
