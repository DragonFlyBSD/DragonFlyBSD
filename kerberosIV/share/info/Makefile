#       @(#)Makefile    8.1 (Berkeley) 6/5/93
# $FreeBSD: src/kerberosIV/share/info/Makefile,v 1.1.1.1.8.2 2002/08/07 16:31:45 ru Exp $
# $DragonFly: src/kerberosIV/share/info/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

NOOBJ=  noobj

all clean cleandir depend lint tags:

beforeinstall:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 dir-tmpl \
		${DESTDIR}${BINDIR}/info/dir-tmpl
	if [ ! -f ${DESTDIR}${BINDIR}/info/dir ]; then \
		${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 dir-tmpl \
			${DESTDIR}${BINDIR}/info/dir ; \
	fi

.include <bsd.prog.mk>
