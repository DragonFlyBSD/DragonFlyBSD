# $NetBSD: src/share/i18n/esdb/ISO646/Makefile.inc,v 1.3 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/esdb/ISO646/Makefile.inc,v 1.1 2005/03/10 16:20:15 joerg Exp $

.PATH: ${.CURDIR}/ISO646

CODE:=		ISO646
.include "${.CURDIR}/Makefile.part"

.for i in ${ISO646_PART}
ISO646-${i:S/:/@/}.src: ISO646.src
	${_MKTARGET_CREATE}
	sed 's/ISO646-x/ISO646-$i/' $> > $@
CLEANFILES:= ${CLEANFILES} ISO646-${i:S/:/@/}.src
.endfor
