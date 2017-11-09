LIBPAMDIR=	${.CURDIR}/../../libpam
MODULE_SHLIB_MAJOR!=grep -w LIB_MAJ ${LIBPAMDIR}/config.h | awk '{print $$3}'
SHLIB_NAME= ${LIB}.so.${MODULE_SHLIB_MAJOR}
SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}
NOINSTALLLIB=	yes
NOPROFILE=	yes

TARGET_LIBDIR=		${LIBDIR}/security
TARGET_SHLIBDIR=	${LIBDIR}/security

CFLAGS+=	-I${LIBPAMDIR}
LDADD+=	-lpam
# XXX workaround for recursive depends
DPADD+= ${LIBPAM:M*libpam.a:R:S/$/.so/}

OPENPAM_DIR=	${.CURDIR}/../../../../contrib/openpam

.include "../Makefile.inc"
