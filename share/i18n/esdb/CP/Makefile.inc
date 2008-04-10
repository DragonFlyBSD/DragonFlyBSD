# $NetBSD: Makefile.inc,v 1.3 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/esdb/CP/Makefile.inc,v 1.2 2008/04/10 10:21:12 hasso Exp $

.PATH: ${.CURDIR}/CP

CODE:=		CP
SEP_CP:=
.include "${.CURDIR}/Makefile.part"

.for i in ${CP_PART}
.if !exists(${.CURDIR}/CP/CP${i:S/:/@/}.src)
CP${i:S/:/@/}.src: CP.src
	${_MKTARGET_CREATE}
	sed 's/CPx/CP$i/' $> > $@
CLEANFILES+= CP${i:S/:/@/}.src
.endif
.endfor
