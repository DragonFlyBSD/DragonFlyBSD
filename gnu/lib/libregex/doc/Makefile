# $FreeBSD: src/gnu/lib/libregex/doc/Makefile,v 1.4.6.1 2002/11/15 18:27:46 ru Exp $
# $DragonFly: src/gnu/lib/libregex/doc/Makefile,v 1.2 2003/06/17 04:25:44 dillon Exp $

INFO = regex
INFOSECTION= "Programming & development tools."
INFOENTRY_regex= "* Regex: (regex).		Regular expression library."

CLEANFILES += regex.texi

regex.texi: xregex.texi ../regex.h
	awk -f ${.CURDIR}/include.awk -v source=${.CURDIR}/../regex.h \
	  < ${.CURDIR}/xregex.texi \
          | expand >$@

.include <bsd.info.mk>
