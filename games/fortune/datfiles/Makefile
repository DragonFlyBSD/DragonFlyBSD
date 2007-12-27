#	@(#)Makefile	8.2 (Berkeley) 4/19/94
# $FreeBSD: src/games/fortune/datfiles/Makefile,v 1.24.2.3 2002/02/18 15:03:32 ru Exp $
# $DragonFly: src/games/fortune/datfiles/Makefile,v 1.7 2007/12/27 13:40:11 matthias Exp $

# list of fortunes to be installed
FORTUNES=	dragonfly-funny dragonfly-tips fortunes fortunes2 fortunes-o \
		murphy startrek zippy

# TO AVOID INSTALLING THE POTENTIALLY OFFENSIVE FORTUNES, COMMENT OUT THE
# THREE LINES AND UNCOMMENT THE FOURTH LINE.

# THE THREE LINES:
FORTUNES+=	fortunes2-o limerick murphy-o
TYPE=		real

# THE FOURTH LINE:
#TYPE=		fake

FILESDIR=	${SHAREDIR}/games/fortune


# add rules for specially handled fortunes here
fortunes-o.dat: fortunes-o
	PATH=$$PATH:/usr/games:${.OBJDIR}/../strfile \
	    strfile -Csx ${.ALLSRC} ${.TARGET}

fortunes-o: fortunes-o.${TYPE}
	tr a-zA-Z n-za-mN-ZA-M < ${.ALLSRC} > ${.TARGET}


.for f in ${FORTUNES}
FILES+=		$f $f.dat
CLEANFILES+=	$f.dat fortunes-o
.if !(target($f.dat))
$f.dat: $f
	PATH=$$PATH:/usr/games:${.OBJDIR}/../strfile \
	    strfile -Cs ${.ALLSRC} ${.TARGET}
.endif
.endfor

.include <bsd.prog.mk>
