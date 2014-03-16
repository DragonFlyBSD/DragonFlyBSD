LIB=		tcplay
MAJ_VER=	2
MIN_VER=	0
SHLIB_MAJOR?=	${MAJ_VER}
SHLIB_MINOR?=	${MIN_VER}
WARNS?=		6
MAN=		tcplay.3

MLINKS+=	tcplay.3	tc_api_init.3
MLINKS+=	tcplay.3	tc_api_uninit.3
MLINKS+=	tcplay.3	tc_api_has.3
MLINKS+=	tcplay.3	tc_api_cipher_iterate.3
MLINKS+=	tcplay.3	tc_api_prf_iterate.3
MLINKS+=	tcplay.3	tc_api_task_init.3
MLINKS+=	tcplay.3	tc_api_task_uninit.3
MLINKS+=	tcplay.3	tc_api_task_set.3
MLINKS+=	tcplay.3	tc_api_task_do.3
MLINKS+=	tcplay.3	tc_api_task_info_get.3
MLINKS+=	tcplay.3	tc_api_task_get_error.3

CFLAGS+=	-DMAJ_VER=${MAJ_VER} -DMIN_VER=${MIN_VER}
CFLAGS+=	-D_FILE_OFFSET_BITS=64

SRCS+=		tcplay.c crc32.c safe_mem.c io.c hdr.c humanize.c
SRCS+=		crypto.c generic_xts.c
SRCS+=		crypto-dev.c pbkdf2-openssl.c
SRCS+=		tcplay_api.c

INCS+=		tcplay_api.h

LDFLAGS+=	-Wl,-version-script=${.CURDIR}/tcplay.map

.include <bsd.lib.mk>
