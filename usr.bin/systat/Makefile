#	@(#)Makefile	8.1 (Berkeley) 6/6/93
#	$DragonFly: src/usr.bin/systat/Makefile,v 1.2 2003/11/08 09:30:32 asmodai Exp $

PROG=	systat
CFLAGS+=-I${.CURDIR}/../../sys
SRCS=	cmds.c cmdtab.c convtbl.c devs.c fetch.c ifcmds.c ifstat.c iostat.c \
	keyboard.c main.c mbufs.c netcmds.c netstat.c pigs.c swap.c icmp.c \
	mode.c ip.c tcp.c vmstat.c
DPADD=	${LIBCURSES} ${LIBTERMCAP} ${LIBM} ${LIBKVM} ${LIBDEVSTAT}
LDADD=	-lcurses -ltermcap -lm -lkvm -ldevstat
BINGRP=	kmem
BINMODE=2555

.include <bsd.prog.mk>
