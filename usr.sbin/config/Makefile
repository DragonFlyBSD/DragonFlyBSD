# 	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/config/Makefile,v 1.26.2.1 2001/04/25 12:09:23 ru Exp $
# $DragonFly: src/usr.sbin/config/Makefile,v 1.4 2005/01/12 01:17:54 cpressey Exp $

PROG=	config
WARNS?=	6
SRCS=	config.y main.c lang.l mkioconf.c mkmakefile.c mkheaders.c \
	mkoptions.c y.tab.h
MAN=	config.8
DPADD=	${LIBL}
LDADD=	-ll

mkmakefile.o: configvers.h

.include <bsd.prog.mk>
