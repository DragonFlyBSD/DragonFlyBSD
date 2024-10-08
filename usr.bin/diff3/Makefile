#	$OpenBSD: Makefile,v 1.2 2003/07/22 00:11:30 millert Exp $

MAN=	diff3.1

beforeinstall:
	install ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/diff3 ${DESTDIR}/usr/bin/

.include <bsd.prog.mk>
