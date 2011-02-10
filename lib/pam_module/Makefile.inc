SHLIB_NAME= ${LIB}.so.${MODULE_SHLIB_MAJOR}
SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}
NOINSTALLLIB=	yes
NOPROFILE=	yes

TARGET_LIBDIR=		/usr/lib/security
TARGET_SHLIBDIR=	/usr/lib/security

LDADD+=	-lpam

OPENPAM_DIR=	${.CURDIR}/../../../contrib/openpam

.include "Makefile.shlib"
