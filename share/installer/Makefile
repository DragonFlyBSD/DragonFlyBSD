# $DragonFly: src/share/installer/Makefile,v 1.1 2008/03/25 22:55:38 dave Exp $

INSTALLER_DIR=	${.CURDIR}/../../contrib/bsdinstaller-1.1.6
DDIR=		${DESTDIR}/usr/share/installer

beforeinstall:
	if [ ! -d ${DDIR} ]; then mkdir ${DDIR}; fi
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 744 cmdnames.conf ${DDIR}/cmdnames.conf
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 744 ${INSTALLER_DIR}/src/frontends/ncurses/fred.txt ${DDIR}/fred.txt

.include <bsd.prog.mk>
