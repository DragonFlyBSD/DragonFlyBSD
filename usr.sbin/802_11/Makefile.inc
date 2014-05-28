CONTRIBDIR= ${.CURDIR}/../../../contrib
WPA_DISTDIR=${CONTRIBDIR}/wpa_supplicant
HOSTAPD_DISTDIR=${CONTRIBDIR}/hostapd

.PATH.c:	${.CURDIR}/.. \
		${WPA_DISTDIR}/src/ap \
		${WPA_DISTDIR}/src/common \
		${WPA_DISTDIR}/src/crypto \
		${WPA_DISTDIR}/src/eap_common \
		${WPA_DISTDIR}/src/eapol_supp \
		${WPA_DISTDIR}/src/l2_packet \
		${WPA_DISTDIR}/src/utils \
		${WPA_DISTDIR}/src/wps

CFLAGS+=-I${.CURDIR}
CFLAGS+=-I${WPA_DISTDIR}/src
CFLAGS+=-I${WPA_DISTDIR}/src/common
CFLAGS+=-I${WPA_DISTDIR}/src/crypto
CFLAGS+=-I${WPA_DISTDIR}/src/l2_packet
CFLAGS+=-I${WPA_DISTDIR}/src/utils

CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX

WARNS?=	1

.include "../Makefile.inc"
