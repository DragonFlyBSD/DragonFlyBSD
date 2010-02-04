# $FreeBSD: src/usr.sbin/wpa/hostapd/Makefile,v 1.3 2007/07/09 16:26:48 sam Exp $
# $DragonFly: src/usr.sbin/802_11/hostapd/Makefile,v 1.2 2007/08/07 11:25:36 sephe Exp $

.include "${.CURDIR}/../Makefile.inc"
.PATH.c:	${.CURDIR}/.. ${HOSTAPD_DISTDIR}/hostapd \
		${HOSTAPD_DISTDIR}/src/eap_server \
		${HOSTAPD_DISTDIR}/src/radius


PROG=	hostapd
SRCS=	accounting.c aes.c aes_wrap.c ap_list.c beacon.c common.c \
	config.c ctrl_iface.c drivers.c eapol_sm.c eap.c eap_common.c \
	eap_identity.c eap_methods.c eloop.c hostapd.c \
	hw_features.c ieee802_11.c ieee802_11_common.c ieee802_11_auth.c \
	ieee802_1x.c ip_addr.c md5.c mlme.c pmksa_cache.c radius.c \
	radius_client.c rc4.c sha1.c sta_info.c vlan_init.c wme.c \
	wpa.c wpa_auth_ie.c wpa_common.c wpa_debug.c wpabuf.c
SRCS+=	l2_packet.c driver_dragonfly.c os_unix.c
SRCS+=	tls_none.c

CLEANFILES=driver_conf.c

MAN=	hostapd.8 hostapd.conf.5

CFLAGS+= -I${HOSTAPD_DISTDIR}/hostapd
CFLAGS+= -I${HOSTAPD_DISTDIR}/src/radius
CFLAGS+= -DCONFIG_DRIVER_BSD
CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX
CFLAGS+= -DEAP_TLS_NONE
CFLAGS+= -DINTERNAL_AES
CFLAGS+= -DINTERNAL_SHA1
CFLAGS+= -DINTERNAL_MD5
DPADD+=	${LIBPCAP}
LDADD+=	-lpcap

driver_conf.c: Makefile
	rm -f driver_conf.c
	echo '/* THIS FILE AUTOMATICALLY GENERATED, DO NOT EDIT! */' \
							> driver_conf.c
	echo '#include <stdlib.h>'			>> driver_conf.c
	echo '#include <stdio.h>'			>> driver_conf.c
	echo '#include <sys/types.h>'			>> driver_conf.c
	echo '#include <netinet/in.h>'			>> driver_conf.c
	echo '#include "hostapd.h"'			>> driver_conf.c
	echo '#include "driver.h"'			>> driver_conf.c
	echo "void bsd_driver_register(void);"		>> driver_conf.c
	echo 'void register_drivers(void) {'		>> driver_conf.c
	echo "bsd_driver_register();"			>> driver_conf.c
	echo '}'					>> driver_conf.c

.include <bsd.prog.mk>
