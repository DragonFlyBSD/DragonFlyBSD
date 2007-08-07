# $FreeBSD: src/usr.sbin/wpa/wpa_supplicant/Makefile,v 1.9 2007/07/11 16:04:08 sam Exp $
# $DragonFly: src/usr.sbin/802_11/wpa_supplicant/Makefile,v 1.2 2007/08/07 11:25:36 sephe Exp $

CONTRIBDIR= ${.CURDIR}/../../../contrib/wpa_supplicant-0.5.8
.PATH:	${.CURDIR}/.. ${CONTRIBDIR}

PROG=	wpa_supplicant
SRCS=	config.c eloop.c common.c md5.c rc4.c sha1.c aes_wrap.c \
	wpa_supplicant.c events.c wpa.c preauth.c pmksa_cache.c \
	ctrl_iface.c ctrl_iface_unix.c l2_packet.c main.c drivers.c \
	Packet32.c os_unix.c
SRCS+=	driver_dragonfly.c driver_wired.c driver_ndis.c
SRCS+=	os_unix.c.patch driver_wired.c.patch events.c.patch

MAN=	wpa_supplicant.8 wpa_supplicant.conf.5

CFLAGS+= -I${.CURDIR} -I${CONTRIBDIR}
CFLAGS+= -DCONFIG_DRIVER_BSD
CFLAGS+= -DCONFIG_DRIVER_NDIS
CFLAGS+= -DCONFIG_DRIVER_WIRED
CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX
CFLAGS+= -DCONFIG_TERMINATE_ONLASTIF
CFLAGS+= -g
DPADD+=	${LIBPCAP}
LDADD+=	-lpcap

# NB: we only support wpa_supplicant.conf file
SRCS+=	config_file.c base64.c
CFLAGS+=-DCONFIG_BACKEND_FILE

.if !defined(NO_WPA_SUPPLICANT_EAPOL)
SRCS+=	eapol_sm.c eap.c eap_methods.c
CFLAGS+= -DIEEE8021X_EAPOL

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL) && !defined(RELEASE_CRUNCH)
CFLAGS+=-DEAP_TLS -DEAP_PEAP -DEAP_MSCHAPv2 -DEAP_LEAP -DEAP_PSK \
	-DEAP_TLV -DEAP_TLS_FUNCS
SRCS+=	eap_tls.c eap_peap.c eap_mschapv2.c eap_leap.c \
	eap_psk.c eap_psk_common.c \
	eap_tlv.c eap_tls_common.c tls_openssl.c ms_funcs.c crypto.c

CFLAGS+=-DEAP_TTLS -DEAP_MD5
SRCS+=	eap_ttls.c eap_md5.c

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
