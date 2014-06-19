# $DragonFly: src/usr.bin/file/Makefile,v 1.9 2007/08/27 16:50:53 pavalos Exp $

CONTRIBDIR=	${.CURDIR}/../../contrib/file
.PATH: ${CONTRIBDIR}/src

LIBMAGICDIR=	${.CURDIR}/../../lib/libmagic
CS=	1
FS=	5
VER!=	grep -w VERSION ${LIBMAGICDIR}/config.h | awk -F\" '{print $$2}'
MAGIC=	/usr/share/misc/magic

PROG=	file
MAN=	file.1 magic.5

CFLAGS+= -DHAVE_CONFIG_H -DMAGIC=\"${MAGIC}\"
CFLAGS+= -I${LIBMAGICDIR}

LDADD+=	-lmagic
DPADD+=	${LIBMAGIC}

file.1: ${CONTRIBDIR}/doc/file.man
	sed -e s@__CSECTION__@${CS}@g	\
	    -e s@__FSECTION__@${FS}@g	\
	    -e s@__VERSION__@${VER}@g	\
	    -e s@__MAGIC__@${MAGIC}@g ${.ALLSRC} > ${.TARGET}

magic.5: ${CONTRIBDIR}/doc/magic.man
	sed -e s@__CSECTION__@${CS}@g	\
	    -e s@__FSECTION__@${FS}@g	\
	    -e s@__VERSION__@${VER}@g	\
	    -e s@__MAGIC__@${MAGIC}@g ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	file.1 magic.5
.include <bsd.prog.mk>
