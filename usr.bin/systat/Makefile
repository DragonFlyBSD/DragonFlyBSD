#	@(#)Makefile	8.1 (Berkeley) 6/6/93
#	$DragonFly: src/usr.bin/systat/Makefile,v 1.7 2008/11/10 04:59:45 swildner Exp $

.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	systat
CFLAGS+=-DINET6 -I${.CURDIR}/../../sys
SRCS=	cmds.c cmdtab.c convtbl.c devs.c fetch.c ifcmds.c ifstat.c iostat.c \
	keyboard.c main.c mbufs.c netcmds.c netstat.c pigs.c sensors.c swap.c \
	icmp.c mode.c ip.c tcp.c vmstat.c ip6.c icmp6.c vmmeter.c symbols.c \
	utmpentry.c netbw.c pftop.c
DPADD=	${LIBCURSES} ${LIBTERMCAP} ${LIBM} ${LIBKVM} ${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-lcurses -ltermcap -lm -lkvm -ldevstat -lkinfo
BINGRP=	kmem
BINMODE=2555

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
