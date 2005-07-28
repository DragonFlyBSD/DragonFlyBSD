# $DragonFly: src/lib/pam_module/Makefile.inc,v 1.3 2005/07/28 19:25:40 joerg Exp $

SHLIB_NAME= ${LIB}.so.${MODULE_SHLIB_MAJOR}
SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}
NOINSTALLLIB=	yes
NOPROFILE=	yes

TARGET_LIBDIR=		/usr/lib/security
TARGET_SHLIBDIR=	/usr/lib/security

LDADD+=	-lpam

OPENPAM_DIR=	${.CURDIR}/../../../contrib/openpam

.include "Makefile.shlib"
