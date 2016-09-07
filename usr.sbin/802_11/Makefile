SUBDIR=	ndis_events wpa_cli wpa_passphrase

.if !defined(NO_LIBRESSL)
SUBDIR+= wpa_supplicant
.endif

SUBDIR_ORDERED=		# maximum parallelism

.include <bsd.subdir.mk>
