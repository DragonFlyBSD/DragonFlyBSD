# $FreeBSD: head/usr.sbin/wpa/wpa_passphrase/Makefile 252726 2013-07-04 21:12:58Z rpaulo $

.include "${.CURDIR}/../Makefile.inc"

.PATH.c:${WPA_DISTDIR}/wpa_supplicant

PROG=	wpa_passphrase
SRCS=	common.c md5-internal.c md5.c os_unix.c sha1-internal.c sha1-pbkdf2.c sha1.c \
	wpa_passphrase.c

CFLAGS+= -DINTERNAL_SHA1
CFLAGS+= -DINTERNAL_MD5

MAN=	wpa_passphrase.8

.include <bsd.prog.mk>
