LIB=	private_ssh
SHLIB_MAJOR= 4
PRIVATELIB=

SRCS=	ssh_api.c \
	ssherr.c \
	sshbuf.c \
	sshkey.c \
	sshbuf-getput-basic.c \
	sshbuf-misc.c \
	sshbuf-getput-crypto.c \
	krl.c \
	bitmap.c
SRCS+=	authfd.c authfile.c bufaux.c bufbn.c bufec.c buffer.c \
	canohost.c channels.c cipher.c \
	cleanup.c \
	compat.c crc32.c fatal.c hostfile.c \
	log.c match.c moduli.c nchan.c packet.c opacket.c \
	readpass.c ttymodes.c xmalloc.c addrmatch.c \
	atomicio.c key.c dispatch.c mac.c uidswap.c uuencode.c misc.c utf8.c \
	monitor_fdpass.c rijndael.c ssh-dss.c ssh-ecdsa.c ssh-rsa.c dh.c \
	msg.c progressmeter.c dns.c entropy.c umac.c umac128.c \
	ssh-pkcs11.c smult_curve25519_ref.c \
	poly1305.c chacha.c cipher-chachapoly.c \
	ssh-ed25519.c digest-openssl.c hmac.c \
	sc25519.c ge25519.c fe25519.c ed25519.c verify.c hash.c blocks.c \
	kex.c kexdh.c kexgex.c kexecdh.c kexc25519.c \
	kexdhc.c kexgexc.c kexecdhc.c kexc25519c.c \
	kexdhs.c kexgexs.c kexecdhs.c kexc25519s.c \
	platform-pledge.c platform-tracing.c platform-misc.c
# Portability layer
SRCS+=	bcrypt_pbkdf.c blowfish.c bsd-misc.c explicit_bzero.c \
	getrrsetbyname.c glob.c openssl-compat.c \
	port-tun.c reallocarray.c realpath.c timingsafe_bcmp.c vis.c \
	recallocarray.c

# only when USE_BUILTIN_RIJNDAEL
#SRCS+=	cipher-aes.c

# only when GSSAPI
#SRCS+=	gss-genr.c

# only when !OPENSSL_HAVE_EVPCTR
#SRCS+=	cipher-ctr.c

# only when !HAVE_EVP_SHA256
#SRCS+=	md-sha256.c

MAN=	moduli.5

WARNS?=	2
NO_STRICT_ALIASING=

.include "../../Makefile.ssh.common"
.include <bsd.lib.mk>
