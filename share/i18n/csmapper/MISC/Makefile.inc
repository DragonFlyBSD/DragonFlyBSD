# $NetBSD: src/share/i18n/csmapper/MISC/Makefile.inc,v 1.5 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/csmapper/MISC/Makefile.inc,v 1.1 2005/03/10 16:19:55 joerg Exp $

.PATH: ${.CURDIR}/MISC

SRCS_mapper.dir+=	mapper.dir.MISC
SRCS_charset.pivot+=	charset.pivot.MISC
CLEANFILES+=		mapper.dir.MISC charset.pivot.MISC
MAPS_MISC=		Big5%UCS.mps \
			UCS%Big5.mps \
			KOI8-R%UCS.mps \
			UCS%KOI8-R.mps \
			KOI8-U%UCS.mps \
			UCS%KOI8-U.mps

mapper.dir.MISC: ${.CURDIR}/MISC/mapper.dir.MISC.src
	${_MKTARGET_CREATE}
	(echo "# MISC" ; cat ${.ALLSRC} ; echo ) > ${.TARGET}

charset.pivot.MISC: ${.CURDIR}/MISC/charset.pivot.MISC.src
	${_MKTARGET_CREATE}
	(echo "# MISC" ; cat ${.ALLSRC} ; echo ) > ${.TARGET}

FILES+= ${MAPS_MISC}
CLEANFILES+= ${MAPS_MISC}
.for i in ${MAPS_MISC}
FILESDIR_$i= ${BINDIR}/MISC
.endfor
