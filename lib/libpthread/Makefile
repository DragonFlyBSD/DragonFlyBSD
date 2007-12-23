# $DragonFly: src/lib/libpthread/Makefile,v 1.5 2007/12/23 02:46:40 sephe Exp $

LIBNAME=	libpthread
SHLIB_MAJOR=	0
SHLIB_NAME=	${LIBNAME}.so
SONAME=		${SHLIB_NAME}.${SHLIB_MAJOR}
DPADD=		${LIBTHREAD}
LDADD=		-l${THREAD_LIB}	# or any lib defining the pthread interface

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
