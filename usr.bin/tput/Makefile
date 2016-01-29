#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/tput/Makefile,v 1.3.6.1 2001/04/25 14:04:33 ru Exp $

PROG=	tput
DPADD=	${LIBTERMCAP}
LDADD=	-lprivate_termcap
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
SCRIPTS=clear.sh
MLINKS=	tput.1 clear.1

.include <bsd.prog.mk>
