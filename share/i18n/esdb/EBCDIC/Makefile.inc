# $NetBSD: src/share/i18n/esdb/EBCDIC/Makefile.inc,v 1.3 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/esdb/EBCDIC/Makefile.inc,v 1.1 2005/03/10 16:19:55 joerg Exp $

.PATH: ${.CURDIR}/EBCDIC

CODE:=		EBCDIC
.include "${.CURDIR}/Makefile.part"

.for i in ${EBCDIC_PART}
EBCDIC-${i:S/:/@/}.src: EBCDIC.src
	${_MKTARGET_CREATE}
	sed 's/EBCDIC-x/EBCDIC-$i/' $> > $@
CLEANFILES:= ${CLEANFILES} EBCDIC-${i:S/:/@/}.src
.endfor
