# $NetBSD: src/share/i18n/esdb/CP/Makefile.inc,v 1.3 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/esdb/CP/Makefile.inc,v 1.1 2005/03/10 16:19:55 joerg Exp $

.PATH: ${.CURDIR}/CP

CODE:=		CP
SEP_CP:=
.include "${.CURDIR}/Makefile.part"

.for i in ${CP_PART}
.if !exists(${.CURDIR}/CP/CP${i:S/:/@/}.src)
CP${i:S/:/@/}.src: CP.src
	sed 's/CPx/CP$i/' ${.ALLSRC} > ${.TARGET}
CLEANFILES+= CP${i:S/:/@/}.src
.endif
.endfor
