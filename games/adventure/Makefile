#	@(#)Makefile	8.1 (Berkeley) 6/12/93
# $FreeBSD: src/games/adventure/Makefile,v 1.7.6.1 2001/04/25 09:28:42 ru Exp $
# $DragonFly: src/games/adventure/Makefile,v 1.5 2005/03/25 12:56:48 liamfoy Exp $

PROG=	adventure
SRCS=	main.c init.c done.c save.c subr.c vocab.c wizard.c io.c data.c crc.c
MAN=	adventure.6
HIDEGAME=hidegame
CLEANFILES=data.c setup.nx setup.no
WARNS?= 6

build-tools: setup.nx

data.c: glorkz setup.nx
	./setup.nx ${.CURDIR}/glorkz > data.c

.include <bsd.prog.mk>
