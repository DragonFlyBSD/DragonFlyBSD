LIB=		tcplay
SHLIB_MAJOR?=	0
WARNS?=		6
MAN=		tcplay.3

MLINKS+=	tcplay.3	tc_api_init.3
MLINKS+=	tcplay.3	tc_api_uninit.3
MLINKS+=	tcplay.3	tc_api_create_volume.3
MLINKS+=	tcplay.3	tc_api_map_volume.3
MLINKS+=	tcplay.3	tc_api_unmap_volume.3
MLINKS+=	tcplay.3	tc_api_check_cipher.3
MLINKS+=	tcplay.3	tc_api_check_prf_hash.3
MLINKS+=	tcplay.3	tc_api_get_error_msg.3
MLINKS+=	tcplay.3	tc_api_get_summary.3


SRCS+=		tcplay_api.c tcplay.c crc32.c safe_mem.c \
		io.c crypto-dev.c hdr.c
INCS+=		tcplay_api.h

LDFLAGS+=	-Wl,-version-script=${.CURDIR}/tcplay.map

.include <bsd.lib.mk>
