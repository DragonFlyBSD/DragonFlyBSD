CRYPTSETUP_DIR=	${.CURDIR}/../../../contrib/cryptsetup

CRYPTSETUP_SRCDIR=	${CRYPTSETUP_DIR}/src
CRYPTSETUP_LUKSDIR=	${CRYPTSETUP_DIR}/luks
CRYPTSETUP_LIBDIR=	${CRYPTSETUP_DIR}/lib
.PATH:  ${CRYPTSETUP_DIR}/man

PROG=		cryptsetup

CSTD?=		gnu89

# Add a backend...
SRCS+=		backends.c libdevmapper.c setup.c utils.c \
		utils_debug.c

SRCS+=		af.c hexprint.c  keyencryption.c keymanage.c \
		pbkdf.c random.c

SRCS+=		cryptsetup.c

MAN=		cryptsetup.8

CFLAGS+=	-I${CRYPTSETUP_SRCDIR} -I${CRYPTSETUP_LIBDIR} \
		-I${CRYPTSETUP_LUKSDIR} -I. -I${CRYPTSETUP_DIR}

LDADD+=		-ldm -lprop -lcrypto
DPADD+=		${LIBDM} ${LIBPROP} ${LIBCRYPTO}

.PATH:	${CRYPTSETUP_SRCDIR}
.PATH:	${CRYPTSETUP_LUKSDIR}
.PATH:	${CRYPTSETUP_LIBDIR}

.include <bsd.prog.mk>
