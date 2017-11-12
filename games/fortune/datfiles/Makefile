#	@(#)Makefile	8.2 (Berkeley) 4/19/94
# $FreeBSD: src/games/fortune/datfiles/Makefile,v 1.24.2.3 2002/02/18 15:03:32 ru Exp $

# list of fortunes to be installed
FORTUNES=	dragonfly-funny dragonfly-tips fortunes murphy startrek zippy

# TO AVOID INSTALLING THE POTENTIALLY OFFENSIVE FORTUNES, RUN 'make' with
# "INSTALL_OFFENSIVE_FORTUNES=NO", or set "INSTALL_OFFENSIVE_FORTUNES=NO"
# IN /etc/make.conf.

INSTALL_OFFENSIVE_FORTUNES?=	YES
.if (${INSTALL_OFFENSIVE_FORTUNES} == "YES")
FORTUNES+=	fortunes-o limerick murphy-o
.endif

FILESDIR=	${SHAREDIR}/games/fortune

.for f in ${FORTUNES}
FILES+=		$f $f.dat
CLEANFILES+=	$f.dat
.if !(target($f.dat))
$f.dat: $f
	${.OBJDIR}/../strfile/strfile.nx -Cs ${.ALLSRC} ${.TARGET}
.endif
.endfor

.include <bsd.prog.mk>
