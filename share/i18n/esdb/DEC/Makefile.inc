# $NetBSD: Makefile.inc,v 1.1 2007/04/01 18:52:31 tnozaki Exp $
# $DragonFly: src/share/i18n/esdb/DEC/Makefile.inc,v 1.1 2008/04/10 10:21:12 hasso Exp $

.PATH: ${.CURDIR}/DEC

CODE:=		DEC
SEP_DEC:=
.include "${.CURDIR}/Makefile.part"

.for i in ${DEC_PART}
.if !exists(${.CURDIR}/DEC/DEC${i:S/:/@/}.src)
DEC${i:S/:/@/}.src: DEC.src
	${_MKTARGET_CREATE}
	sed 's/DECx/DEC$i/' $> > $@
CLEANFILES+=	DEC${i:S/:/@/}.src
.endif
.endfor
