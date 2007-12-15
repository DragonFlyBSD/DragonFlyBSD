# $DragonFly: src/lib/libpthread/Makefile,v 1.4 2007/12/15 07:13:18 sephe Exp $

LIBNAME=	libpthread
SHLIB_MAJOR=	0
SHLIB_NAME=	${LIBNAME}.so
SONAME=		${SHLIB_NAME}.${SHLIB_MAJOR}
DEFAULT_PTHREAD_LIB?=	thread_xu
# XXX
#DPADD=		${DESTDIR}${LIBDIR}/lib${DEFAULT_PTHREAD_LIB}.a
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
