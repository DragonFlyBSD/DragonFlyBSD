# $FreeBSD: src/bin/csh/nls/ja/Makefile,v 1.4.2.4 2002/08/07 16:31:38 ru Exp $
# $DragonFly: src/bin/csh/nls/ja/Attic/Makefile,v 1.2 2003/06/17 04:22:49 dillon Exp $

NL=     ja
DL=     ja_JP.EUC

.PATH: ${BASESRC}/${NL}

tcsh.msg: set[0-9]*
	cat ${BASESRC}/${NL}/set[0-9] \
	    ${BASESRC}/${NL}/set[0-9][0-9] > ${.TARGET}

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
		tcsh.cat ${DESTDIR}${NLSDIR}/${DL}/tcsh.cat

.include <bsd.prog.mk>
