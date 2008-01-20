# $DragonFly: src/lib/libpthread/Makefile,v 1.6 2008/01/20 19:14:09 corecode Exp $

LIBNAME=	libpthread
SHLIB_MAJOR=	0
SHLIB_NAME=	${LIBNAME}.so
SONAME=		${SHLIB_NAME}.${SHLIB_MAJOR}

SRCS=		dummy.c

realinstall: linkinstall
linkinstall:
	# Do not overwrite existing symlinks
	if [ ! -e ${DESTDIR}${TARGET_SHLIBDIR}/${SONAME} ]; then \
		${LN} -fs lib${THREAD_LIB}.so \
			${DESTDIR}${TARGET_SHLIBDIR}/${SONAME}; \
	fi
	if [ ! -e ${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a ]; then \
		${LN} -fs lib${THREAD_LIB}.a \
			${DESTDIR}${TARGET_LIBDIR}/${LIBNAME}.a; \
	fi

.include <bsd.lib.mk>
