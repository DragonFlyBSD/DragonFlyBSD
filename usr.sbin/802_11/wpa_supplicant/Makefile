# $FreeBSD: src/usr.sbin/wpa/wpa_supplicant/Makefile,v 1.9 2007/07/11 16:04:08 sam Exp $
# $DragonFly: src/usr.sbin/802_11/wpa_supplicant/Makefile,v 1.2 2007/08/07 11:25:36 sephe Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH.c:	${WPA_DISTDIR}/wpa_supplicant \
		${WPA_DISTDIR}/src/drivers \
		${WPA_DISTDIR}/src/eap_peer \
		${WPA_DISTDIR}/src/rsn_supp

PROG=	wpa_supplicant
SRCS=	aes.c aes_wrap.c blacklist.c common.c config.c ctrl_iface.c \
	ctrl_iface_unix.c drivers.c eloop.c events.c l2_packet.c main.c \
	md5.c preauth.c pmksa_cache.c rc4.c scan.c scan_helpers.c sha1.c \
	wpa.c wpa_common.c wpa_debug.c wpa_ie.c wpa_supplicant.c \
	wpabuf.c wpas_glue.c \
	driver_ndis.c Packet32.c \
	driver_wired.c \
	driver_dragonfly.c os_unix.c
#SRCS+=	driver_wired.c.patch events.c.patch

MAN=	wpa_supplicant.8 wpa_supplicant.conf.5

CFLAGS+= -I${.CURDIR} -I${WPA_DISTDIR}
CFLAGS+= -I${WPA_DISTDIR}/src/drivers
CFLAGS+= -I${WPA_DISTDIR}/src/rsn_supp
CFLAGS+= -DCONFIG_DRIVER_BSD
CFLAGS+= -DCONFIG_DRIVER_NDIS
CFLAGS+= -DCONFIG_DRIVER_WIRED
CFLAGS+= -DCONFIG_TERMINATE_ONLASTIF
CFLAGS+= -g
DPADD+=	${LIBPCAP}
LDADD+=	-lpcap

# NB: we only support wpa_supplicant.conf file
SRCS+=	config_file.c base64.c
CFLAGS+=-DCONFIG_BACKEND_FILE

.if !defined(NO_WPA_SUPPLICANT_EAPOL)
.PATH: ${.CURDIR}/.. ${WPA_DISTDIR}/src/eapol_supp
.PATH: ${.CURDIR}/.. ${WPA_DISTDIR}/src/eap_peer
SRCS+=	eapol_supp_sm.c eap.c eap_common.c eap_methods.c
CFLAGS+= -DIEEE8021X_EAPOL

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
.PATH:  ${.CURDIR}/.. ${WPA_DISTDIR}/src/eap_common
.PATH:  ${.CURDIR}/.. ${WPA_DISTDIR}/eap_peer/
CFLAGS+=-DEAP_TLS -DEAP_PEAP -DEAP_MSCHAPv2 -DEAP_LEAP -DEAP_PSK \
	-DEAP_TLV -DEAP_TLS_FUNCS
SRCS+=	chap.c crypto_openssl.c \
	eap_leap.c \
	eap_mschapv2.c \
	eap_peap.c eap_peap_common.c \
	eap_psk.c eap_psk_common.c \
	eap_tls.c eap_tls_common.c \
	mschapv2.c ms_funcs.c tls_openssl.c

CFLAGS+=-DEAP_TTLS -DEAP_MD5
SRCS+=	eap_ttls.c eap_md5.c

.if !empty(CFLAGS:M*-DEAP_GTC)
SRCS+=	eap_gtc.c
.endif

# NB: requires patch to openssl
#CFLAGS+= -DEAP_FAST
#SRCS+=	eap_fast.c

DPADD+= ${LIBSSL} ${LIBCRYPTO}
LDADD+= -lssl -lcrypto
.else
SRCS+=	tls_none.c
.endif

.endif

.include <bsd.prog.mk>
