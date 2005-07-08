# $DragonFly: src/lib/pam_module/Makefile.inc,v 1.1 2005/07/08 15:42:01 joerg Exp $

SHLIB_NAME= ${LIB}.so.${MODULE_SHLIB_MAJOR}
SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}
NOINSTALLLIB=	yes
NOPROFILE=	yes

LIBDIR=		/usr/lib/security

LDADD+=	-lpam
DPADD+=	${LIBPAM}

OPENPAM_DIR=	${.CURDIR}/../../../contrib/openpam

.include "Makefile.shlib"
