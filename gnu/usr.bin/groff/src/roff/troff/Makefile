# $FreeBSD: src/gnu/usr.bin/groff/src/roff/troff/Makefile,v 1.4 2003/05/01 13:22:20 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/roff/troff/Makefile,v 1.4 2005/09/09 14:11:04 asmodai Exp $

PROG_CXX=	troff
SRCS=		column.cpp dictionary.cpp div.cpp env.cpp input.cpp mtsm.cpp\
		node.cpp number.cpp reg.cpp majorminor.cpp
DPADD=		${LIBGROFF} ${LIBM}
LDADD=		${LIBGROFF} -lm
CLEANFILES=	majorminor.cpp ${MAN}

.include <bsd.prog.mk>

majorminor.cpp: ${GROFF_DIST}/VERSION ${GROFF_DIST}/REVISION
	@${ECHO} Making $@
	@-rm -f $@
	@echo const char \*major_version = \
	\"`sed -e 's/^\([^.]*\)\..*$$/\1/' ${GROFF_DIST}/VERSION`\"\; >$@
	@echo const char \*minor_version = \
	\"`sed -e 's/^[^.]*\.\([0-9]*\).*$$/\1/' ${GROFF_DIST}/VERSION`\"\; >>$@
	@echo const char \*revision = \"`cat ${GROFF_DIST}/REVISION`\"\; >>$@
