.PATH: ${.CURDIR}/../boggle

# XXX better word list selection
.if exists(${.CURDIR}/../../../share/dict/web2)
BOOGLEWORDS?=	${.CURDIR}/../../../share/dict/web2
.else
BOOGLEWORDS?=	/usr/share/dict/words
.endif

FILES=	dictindex dictionary helpfile
FILESDIR=	${SHAREDIR}/games/boggle

CLEANFILES=	dictindex dictionary

dictionary:
	${.OBJDIR}/../mkdict/mkdict.nx < ${BOOGLEWORDS} > ${.TARGET}

dictindex: dictionary
	${.OBJDIR}/../mkindex/mkindex.nx < ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
