#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/tput/Makefile,v 1.3.6.1 2001/04/25 14:04:33 ru Exp $
# $DragonFly: src/usr.bin/tput/Makefile,v 1.3 2005/02/04 20:00:25 cpressey Exp $

PROG=	tput
WARNS?=	6
DPADD=	${LIBTERMCAP}
LDADD=	-ltermcap
SCRIPTS=clear.sh
MLINKS=	tput.1 clear.1

.include <bsd.prog.mk>
