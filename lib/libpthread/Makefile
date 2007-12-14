# $DragonFly: src/lib/libpthread/Makefile,v 1.3 2007/12/14 20:27:14 corecode Exp $

LIBNAME=	libpthread
SHLIB_MAJOR=	0
SHLIB_NAME=	${LIBNAME}.so
SONAME=		${SHLIB_NAME}.${SHLIB_MAJOR}
DEFAULT_PTHREAD_LIB?=	libthread_xu
LDADD=		-l${DEFAULT_PTHREAD_LIB}	# or any lib defining the pthread interface

SRCS=		dummy.c

realinstall: linkinstall
linkinstall:
	# Do not overwrite existing symlinks
	if [ ! -e ${DESTDIR}${TARGET_SHLIBDIR}/${SONAME} ]; then \
		${LN} -fs lib${DEFAULT_PTHREAD_LIB}.so \
			${DESTDIR}${TARGET_SHLIBDIR}/${SONAME}; \
	fi
	if [ ! -e ${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a ]; then \
		${LN} -fs lib${DEFAULT_PTHREAD_LIB}.a \
			${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a; \
	fi

.include <bsd.lib.mk>
