# $FreeBSD: src/bin/csh/nls/et/Makefile,v 1.1.2.2 2002/08/07 16:31:37 ru Exp $
# $DragonFly: src/bin/csh/nls/et/Attic/Makefile,v 1.2 2003/06/17 04:22:49 dillon Exp $

NL=     et
DL=     et_EE.ISO8859-15

.PATH: ${BASESRC}/${NL}

tcsh.msg: set[0-9]*
	cat ${BASESRC}/${NL}/set[0-9] \
	    ${BASESRC}/${NL}/set[0-9][0-9] > ${.TARGET}

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
		tcsh.cat ${DESTDIR}${NLSDIR}/${DL}/tcsh.cat

.include <bsd.prog.mk>
