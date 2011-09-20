# $FreeBSD: src/secure/lib/libssh/Makefile,v 1.2.2.7 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/lib/libssh/Makefile,v 1.13 2008/09/28 17:18:02 pavalos Exp $

LIB=	ssh
SHLIB_MAJOR= 4
SRCS=	acss.c authfd.c authfile.c bufaux.c bufbn.c buffer.c \
	canohost.c channels.c cipher.c cipher-acss.c cipher-aes.c \
	cipher-bf1.c cipher-ctr.c cipher-3des1.c cleanup.c \
	compat.c compress.c crc32.c deattack.c fatal.c hostfile.c \
	log.c match.c md-sha256.c moduli.c nchan.c packet.c \
	readpass.c rsa.c ttymodes.c xmalloc.c addrmatch.c \
	atomicio.c key.c dispatch.c kex.c mac.c uidswap.c uuencode.c misc.c \
	monitor_fdpass.c rijndael.c ssh-dss.c ssh-ecdsa.c ssh-rsa.c dh.c \
	kexdh.c kexgex.c kexdhc.c kexgexc.c bufec.c kexecdh.c kexecdhc.c \
	msg.c progressmeter.c dns.c entropy.c gss-genr.c umac.c jpake.c \
	schnorr.c ssh-pkcs11.c
# Portability layer
SRCS+=	bsd-misc.c getrrsetbyname.c glob.c openssl-compat.c \
	port-tun.c timingsafe_bcmp.c
# FreeBSD additions
SRCS+=	version.c

MAN=	moduli.5

WARNS?=	0

.include "../../Makefile.ssh.common"
.include <bsd.lib.mk>

.PATH: ${SSHDIR}/openbsd-compat
