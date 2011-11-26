# $NetBSD: Makefile.inc,v 1.1 2006/06/15 15:34:20 tnozaki Exp $
# $DragonFly: src/share/i18n/esdb/GEORGIAN/Makefile.inc,v 1.1 2008/04/10 10:21:12 hasso Exp $

.PATH: ${.CURDIR}/GEORGIAN

CODE:=		GEORGIAN
SEP_GEORGIAN:=	-
.include "${.CURDIR}/Makefile.part"

.for i in ${GEORGIAN_PART}
GEORGIAN-${i}.src: GEORGIAN.src
	${_MKTARGET_CREATE}
	sed -e 's/GEORGIANx/GEORGIAN-${i}/' \
	    -e 's/GEORGIANy/GEORGIAN-${i:S/-/:/}/' $> > $@
CLEANFILES+= GEORGIAN-${i}.src
.endfor
