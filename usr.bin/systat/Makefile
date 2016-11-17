#	@(#)Makefile	8.1 (Berkeley) 6/6/93

.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	systat
NO_STRICT_ALIASING=
CFLAGS+=-DINET6 -I${.CURDIR}/../../sys
SRCS=	cmds.c cmdtab.c convtbl.c devs.c fetch.c ifcmds.c ifstat.c iostat.c \
	keyboard.c main.c mbufs.c netcmds.c netstat.c pigs.c sensors.c swap.c \
	icmp.c mode.c ip.c tcp.c vmstat.c ip6.c icmp6.c vmmeter.c symbols.c \
	utmpentry.c netbw.c pftop.c altqs.c
DPADD=	${LIBNCURSES} ${LIBNCURSES} ${LIBM} ${LIBKVM} ${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-lprivate_ncurses -lprivate_ncurses -lm -lkvm -ldevstat -lkinfo
BINGRP=	kmem
BINMODE=2555


DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
