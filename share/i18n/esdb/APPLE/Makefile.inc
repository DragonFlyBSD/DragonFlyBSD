# $NetBSD: Makefile.inc,v 1.1 2006/03/13 19:45:36 tnozaki Exp $
# $DragonFly: src/share/i18n/esdb/APPLE/Makefile.inc,v 1.1 2008/04/10 10:21:11 hasso Exp $

.PATH: ${.CURDIR}/APPLE

CODE:=		MAC
PARTFILE_MAC:=	${.CURDIR}/APPLE/MAC.part
ALIASFILE_MAC:=	${.CURDIR}/APPLE/MAC.alias
SEP_MAC:=
SUBDIR_MAC:=	APPLE
.include "${.CURDIR}/Makefile.part"

.for i in ${MAC_PART}
.if !exists(${.CURDIR}/MAC/MAC${i:S/:/@/}.src)
MAC${i:S/:/@/}.src: MAC.src
	${_MKTARGET_CREATE}
	sed 's/changeme/$i/' $> > $@
CLEANFILES+= MAC${i:S/:/@/}.src
.endif
.endfor
