#	@(#)Makefile	5.4 (Berkeley) 5/11/90
#
# $FreeBSD: src/sbin/slattach/Makefile,v 1.9.2.1 2001/04/25 10:58:52 ru Exp $
# $DragonFly: src/sbin/slattach/Makefile,v 1.3 2005/02/02 07:59:48 cpressey Exp $

PROG=	slattach
WARNS?=	6
MAN=	slattach.8
MLINKS=	slattach.8 slip.8
LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>
