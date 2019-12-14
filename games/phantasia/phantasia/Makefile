#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/phantasia/Makefile,v 1.16.2.2 2002/08/07 16:31:42 ru Exp $

.PATH: ${.CURDIR}/..

PROG=	phantasia
MAN=	phantasia.6
SRCS=	main.c fight.c io.c interplayer.c gamesupport.c misc.c phantglobs.c
DPADD=	${LIBM}
LDADD=	-lm
VARGAMES=
GAMESCURSES=

DATAFILES=	characs gold lastdead mess motd scoreboard void
CLEANFILES=	${DATAFILES} map monsters

FILES=		monsters
FILESMODE=	660
FILESGRP=	${BINGRP}
FILESDIR=	/var/games/phantasia

.ORDER: monsters ${DATAFILES}
monsters ${DATAFILES}: monsters.asc setup.c
	${.OBJDIR}/../setup/setup.nx -m ${.CURDIR}/../monsters.asc

beforeinstall:
.for file in ${DATAFILES}
.if !exists(${DESTDIR}/var/games/phantasia/${file})
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 660 \
	    ${file} ${DESTDIR}/var/games/phantasia
.endif
.endfor

# Make Phantasia map.  Change the map commands reflect your installation.
# PLOTDEVICE is used for plotting the map.  Change as appropriate.
# Requires dports graphics/plotutils
map: map.c
	${CC} -static -O ${.CURDIR}/../map.c -L/usr/local/lib -lplot -o ${.TARGET}
	./map | plot > /dev/tty

.include <bsd.prog.mk>
