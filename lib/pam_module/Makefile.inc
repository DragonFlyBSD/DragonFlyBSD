# $DragonFly: src/lib/pam_module/Makefile.inc,v 1.2 2005/07/12 23:30:24 joerg Exp $

SHLIB_NAME= ${LIB}.so.${MODULE_SHLIB_MAJOR}
SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}
NOINSTALLLIB=	yes
NOPROFILE=	yes

LIBDIR=		/usr/lib/security

LDADD+=	-lpam

OPENPAM_DIR=	${.CURDIR}/../../../contrib/openpam

.include "Makefile.shlib"
