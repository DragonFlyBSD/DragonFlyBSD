#	$FreeBSD: src/tools/tools/net80211/Makefile,v 1.6 2009/01/12 22:50:55 sam Exp $

SUBDIR=	stumbler wesside wlaninject wlanstats wlantxtime wlanwatch wlanwds

.if !defined(NO_LIBRESSL)
SUBDIR+= w00t
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
