# $FreeBSD: src/gnu/lib/libg++/doc/Makefile,v 1.5 1999/08/27 23:32:46 peter Exp $
# $DragonFly: src/gnu/lib/libg++/doc/Attic/Makefile,v 1.2 2003/06/17 04:25:44 dillon Exp $

INFO = g++FAQ libg++ iostream
INFOSECTION= "Gcc Documentation"
INFOENTRY_g++FAQ= "* G++FAQ: (g++FAQ).		The GNU G++ FAQ."

.PATH: ${.CURDIR}/../../../../contrib/libg++/libg++
.PATH: ${.CURDIR}/../../../../contrib/libg++/libio

# HACK ALERT!
CLEANFILES+= lgpl.texinfo
libg++.info:	lgpl.texinfo
lgpl.texinfo:
	echo "See the file: /usr/src/gnu/COPYING.LIB" > lgpl.texinfo

.include <bsd.info.mk>
