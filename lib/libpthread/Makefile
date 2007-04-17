# $DragonFly: src/lib/libpthread/Makefile,v 1.1 2007/04/17 12:34:07 corecode Exp $

LIBNAME=	libpthread
SHLIB_MAJOR=	0
SHLIB_NAME=	${LIBNAME}.so
SONAME=		${SHLIB_NAME}.${SHLIB_MAJOR}
DEFAULT_PTHREAD_LIB?=	c_r
LDADD=		-l${DEFAULT_PTHREAD_LIB}	# or any lib defining the pthread interface

SRCS=		dummy.c

realinstall: linkinstall
linkinstall:
	# Do not overwrite existing symlinks
	if [ ! -e ${DESTDIR}${TARGET_SHLIBDIR}/${LIBNAME}.so ]; then \
		${LN} -fs lib${DEFAULT_PTHREAD_LIB}.so \
			${DESTDIR}${TARGET_SHLIBDIR}/${LIBNAME}.so; \
	fi
	if [ ! -e ${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a ]; then \
		${LN} -fs lib${DEFAULT_PTHREAD_LIB}.a \
			${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a; \
	fi

.include <bsd.lib.mk>
