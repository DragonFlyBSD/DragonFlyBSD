# $FreeBSD: head/usr.sbin/wpa/wpa_passphrase/Makefile 252726 2013-07-04 21:12:58Z rpaulo $

.include "${.CURDIR}/../Makefile.inc"
.include "${.CURDIR}/../Makefile.crypto"

.PATH.c:${WPA_DISTDIR}/wpa_supplicant

PROG=	wpa_passphrase
SRCS=	common.c crypto_openssl.c os_unix.c sha1-prf.c sha1-tlsprf.c sha256-prf.c \
	sha256-tlsprf.c wpabuf.c wpa_debug.c wpa_passphrase.c

CFLAGS+= -DCONFIG_OPENSSL_INTERNAL_AES_WRAP
CFLAGS+= -DINTERNAL_SHA1
CFLAGS+= -DINTERNAL_MD5

MAN=	wpa_passphrase.8

.include <bsd.prog.mk>
