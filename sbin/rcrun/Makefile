# $DragonFly: src/sbin/rcrun/Makefile,v 1.3 2007/10/13 23:51:32 swildner Exp $

SCRIPTS=rcrun.sh
SYMLINKS= \
	rcrun ${BINDIR}/rcstart \
	rcrun ${BINDIR}/rcstop \
	rcrun ${BINDIR}/rcrestart \
	rcrun ${BINDIR}/rcvar \
	rcrun ${BINDIR}/rclist \
	rcrun ${BINDIR}/rcforce \
	rcrun ${BINDIR}/rcfast \
	rcrun ${BINDIR}/rcone \
	rcrun ${BINDIR}/rcenable \
	rcrun ${BINDIR}/rcdisable

MAN=	rcrun.8
MLINKS=	rcrun.8 rcdisable.8 \
	rcrun.8 rcenable.8 \
	rcrun.8 rcfast.8 \
	rcrun.8 rcforce.8 \
	rcrun.8 rcone.8 \
	rcrun.8 rclist.8 \
	rcrun.8 rcrestart.8 \
	rcrun.8 rcstart.8 \
	rcrun.8 rcstop.8 \
	rcrun.8 rcvar.8

.include <bsd.prog.mk>
