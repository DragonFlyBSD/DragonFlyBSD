#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: head/usr.bin/ul/Makefile 87700 2001-12-11 23:27:23Z markm $

PROG=	ul

DPADD=	${LIBTERMCAP}
LDADD=	-lprivate_termcap
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
