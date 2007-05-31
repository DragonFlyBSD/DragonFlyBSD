#	@(#)Makefile	8.1 (Berkeley) 6/6/93
#	$DragonFly: src/usr.bin/systat/Makefile,v 1.4 2007/05/31 11:38:36 hasso Exp $

PROG=	systat
CFLAGS+=-DINET6 -I${.CURDIR}/../../sys
SRCS=	cmds.c cmdtab.c convtbl.c devs.c fetch.c ifcmds.c ifstat.c iostat.c \
	keyboard.c main.c mbufs.c netcmds.c netstat.c pigs.c swap.c icmp.c \
	mode.c ip.c tcp.c vmstat.c ip6.c icmp6.c
DPADD=	${LIBCURSES} ${LIBTERMCAP} ${LIBM} ${LIBKVM} ${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-lcurses -ltermcap -lm -lkvm -ldevstat -lkinfo
BINGRP=	kmem
BINMODE=2555

.include <bsd.prog.mk>
