#	@(#)Makefile	8.1 (Berkeley) 6/12/93
# $FreeBSD: src/games/adventure/Makefile,v 1.7.6.1 2001/04/25 09:28:42 ru Exp $
# $DragonFly: src/games/adventure/Makefile,v 1.4 2004/07/27 07:41:03 asmodai Exp $

PROG=	adventure
SRCS=	main.c init.c done.c save.c subr.c vocab.c wizard.c io.c data.c crc.c
MAN=	adventure.6
HIDEGAME=hidegame
CLEANFILES=data.c setup.nx setup.no

build-tools: setup.nx

data.c: glorkz setup.nx
	./setup.nx ${.CURDIR}/glorkz > data.c

.include <bsd.prog.mk>
