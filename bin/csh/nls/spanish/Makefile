# $FreeBSD: src/bin/csh/nls/spanish/Makefile,v 1.4.2.3 2002/08/07 16:31:39 ru Exp $
# $DragonFly: src/bin/csh/nls/spanish/Attic/Makefile,v 1.2 2003/06/17 04:22:49 dillon Exp $

NL=     spanish
DL=     es_ES.ISO8859-1
LL=     es_ES.ISO8859-15

.PATH: ${BASESRC}/${NL}

tcsh.msg: set[0-9]*
	cat ${BASESRC}/${NL}/set[0-9] \
	    ${BASESRC}/${NL}/set[0-9][0-9] > ${.TARGET}

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
		tcsh.cat ${DESTDIR}${NLSDIR}/${DL}/tcsh.cat
.for i in ${LL}
	ln -fs ../${DL}/tcsh.cat ${DESTDIR}${NLSDIR}/$i/tcsh.cat
.endfor

.include <bsd.prog.mk>
