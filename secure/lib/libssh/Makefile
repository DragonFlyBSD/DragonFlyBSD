LIB=	ssh
SHLIB_MAJOR= 4
SRCS=	ssherr.c \
	sshbuf.c \
	sshkey.c \
	sshbuf-getput-basic.c \
	sshbuf-misc.c \
	sshbuf-getput-crypto.c
SRCS+=	authfd.c authfile.c bufaux.c bufbn.c buffer.c \
	canohost.c channels.c cipher.c cipher-aes.c \
	cipher-bf1.c cipher-ctr.c cipher-ctr-mt.c cipher-3des1.c cleanup.c \
	compat.c compress.c crc32.c deattack.c fatal.c hostfile.c \
	log.c match.c md-sha256.c moduli.c nchan.c packet.c \
	readpass.c rsa.c ttymodes.c xmalloc.c addrmatch.c \
	atomicio.c key.c dispatch.c kex.c mac.c uidswap.c uuencode.c misc.c \
	monitor_fdpass.c rijndael.c ssh-dss.c ssh-ecdsa.c ssh-rsa.c dh.c \
	kexdh.c kexgex.c kexdhc.c kexgexc.c bufec.c kexecdh.c kexecdhc.c \
	msg.c progressmeter.c dns.c entropy.c gss-genr.c umac.c umac128.c \
	ssh-pkcs11.c krl.c smult_curve25519_ref.c \
	kexc25519.c kexc25519c.c poly1305.c chacha.c cipher-chachapoly.c \
	ssh-ed25519.c digest-openssl.c hmac.c \
	sc25519.c ge25519.c fe25519.c ed25519.c verify.c hash.c blocks.c
# Portability layer
SRCS+=	bcrypt_pbkdf.c blowfish.c bsd-misc.c explicit_bzero.c \
	getrrsetbyname.c glob.c openssl-compat.c \
	port-tun.c timingsafe_bcmp.c

MAN=	moduli.5

WARNS?=	2

.include "../../Makefile.ssh.common"
.include <bsd.lib.mk>
