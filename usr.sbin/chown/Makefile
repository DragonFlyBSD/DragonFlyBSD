#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/chown/Makefile,v 1.5.2.1 2001/04/25 12:09:22 ru Exp $
# $DragonFly: src/usr.sbin/chown/Makefile,v 1.3 2004/11/25 19:45:30 liamfoy Exp $

PROG=	chown
CFLAGS+=-DSUPPORT_DOT
MAN=	chgrp.1 chown.8
LINKS=	${BINDIR}/chown /usr/bin/chgrp

WARNS?=	6
.include <bsd.prog.mk>
