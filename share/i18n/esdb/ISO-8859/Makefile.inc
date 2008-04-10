# $NetBSD: Makefile.inc,v 1.3 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/esdb/ISO-8859/Makefile.inc,v 1.2 2008/04/10 10:21:12 hasso Exp $

.PATH: ${.CURDIR}/ISO-8859

CODE:=		ISO-8859
.include "${.CURDIR}/Makefile.part"

.for i in ${ISO-8859_PART}
ISO-8859-$i.src: ISO-8859.src
	${_MKTARGET_CREATE}
	sed 's/ISO-8859-x/ISO-8859-$i/' $> > $@
CLEANFILES+= ISO-8859-$i.src
.endfor
