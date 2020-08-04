LIB=	private_ssh
SHLIB_MAJOR= 5
PRIVATELIB= shpub

.PATH: ${.CURDIR}/../../crypto/openssh
SRCS=	addrmatch.c atomicio.c authfd.c authfile.c bitmap.c canohost.c \
	chacha.c channels.c cipher.c cipher-chachapoly-libcrypto.c cleanup.c compat.c \
	dh.c digest-openssl.c dispatch.c dns.c entropy.c fatal.c \
	hash.c hmac.c hostfile.c \
	krl.c log.c mac.c match.c misc.c moduli.c monitor_fdpass.c msg.c \
	nchan.c packet.c platform-misc.c poly1305.c readpass.c \
	ssh_api.c ssh-dss.c ssh-ecdsa.c ssh-ecdsa-sk.c ssh-ed25519.c ssh-ed25519-sk.c \
	ssh-rsa.c ssh-sk-client.c sshbuf.c sshbuf-getput-basic.c sshbuf-getput-crypto.c \
	sshbuf-io.c sshbuf-misc.c \
	ssherr.c sshkey.c sshsig.c ttymodes.c \
	umac.c umac128.c utf8.c verify.c xmalloc.c
SRCS+=	ed25519.c fe25519.c ge25519.c sc25519.c smult_curve25519_ref.c
SRCS+=	kex.c kexc25519.c kexdh.c kexecdh.c \
	kexgen.c kexgex.c kexgexc.c kexgexs.c \
	kexsntrup4591761x25519.c sntrup4591761.c

# not implemented: platform-pledge.c platform-tracing.c
# only for scp/sftp: progressmeter.c
# disabled: ssh-pkcs11.c
# no GSSAPI: gss-genr.c
# no openssl: cipher-aes.c cipher-aesctr.c cipher-ctr.c crc32.c digest-libc.c rijndael.c
# no xmss: ssh-xmss.c sshkey-xmss.c xmss_commons.c xmss_fast.c xmss_hash.c
#	   xmss_hash_address.c xmss_wots.c

# Portability layer
.PATH: ${.CURDIR}/openbsd-compat
SRCS+=	bcrypt_pbkdf.c blowfish.c bsd-misc.c bsd-signal.c fmt_scaled.c \
	getrrsetbyname-ldns.c glob.c openssl-compat.c port-net.c xcrypt.c

# not needed compat:
# arc4random.c base64.c basename.c bindresvport.c bsd-asprintf.c
# bsd-closefrom.c bsd-cygwin_util.c bsd-err.c bsd-flock.c bsd-getline.c
# bsd-getpagesize.c bsd-getpeereid.c bsd-malloc.c bsd-nextstep.c bsd-openpty.c
# bsd-poll.c bsd-setres_id.c bsd-snprintf.c bsd-statvfs.c bsd-waitpid.c
# daemon.c dirname.c explicit_bzero.c fake-rfc2553.c freezero.c getcwd.c
# getgrouplist.c getopt_long.c inet_aton.c inet_ntoa.c inet_ntop.c
# kludge-fd_set.c libressl-api-compat.c md5.c mktemp.c
# port-aix.c port-irix.c port-linux.c port-solaris.c port-uw.c
# pwcache.c readpassphrase.c reallocarray.c recallocarray.c rmd160.c
# rresvport.c setenv.c setproctitle.c sha1.c sha2.c sigact.c
# strcasestr.c strlcat.c strlcpy.c strmode.c strndup.c strnlen.c strptime.c
# strsep.c strtoll.c strtonum.c strtoul.c strtoull.c timingsafe_bcmp.c vis.c

MAN=	moduli.5

WARNS?=	2
NO_STRICT_ALIASING=

CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+=	-I${.CURDIR}/../../contrib/ldns
DPADD=		${LIBCRYPTO} ${LIBLDNS} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_crypto -lprivate_ldns -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.lib.mk>
