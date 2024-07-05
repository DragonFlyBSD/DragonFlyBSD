LIB=	private_ssh
SHLIB_MAJOR= 5
PRIVATELIB= shpub

.PATH: ${.CURDIR}/../../crypto/openssh

# SRCS built from /usr/src/crypto/openssh/Makefile.inc,
#	XMSS_OBJS
#	LIBOPENSSH_OBJS
#	LIBSHS_OBJS
#	+ ssh-sk-client.c
#
#	ssh-sk-client.c is used by sshkey.c from LIBOPENSSH_OBJS and had
#	to be included in libssh in order for initrd rescue to build.
#
SRCS=   ssh_api.c \
        ssherr.c \
        sshbuf.c \
        sshkey.c \
        sshbuf-getput-basic.c \
        sshbuf-misc.c \
        sshbuf-getput-crypto.c \
        krl.c \
        bitmap.c \
	\
        ssh-xmss.c \
        sshkey-xmss.c \
        xmss_commons.c \
        xmss_fast.c \
        xmss_hash.c \
        xmss_hash_address.c \
        xmss_wots.c \
	\
        authfd.c authfile.c \
        canohost.c channels.c cipher.c cipher-aes.c cipher-aesctr.c \
        cleanup.c \
        compat.c fatal.c hostfile.c \
        log.c match.c moduli.c nchan.c packet.c \
        readpass.c ttymodes.c xmalloc.c addr.c addrmatch.c \
        atomicio.c dispatch.c mac.c misc.c utf8.c \
        monitor_fdpass.c rijndael.c ssh-dss.c ssh-ecdsa.c ssh-ecdsa-sk.c \
        ssh-ed25519-sk.c ssh-rsa.c dh.c \
        msg.c progressmeter.c dns.c entropy.c gss-genr.c umac.c umac128.c \
        ssh-pkcs11.c smult_curve25519_ref.c \
        poly1305.c chacha.c cipher-chachapoly.c cipher-chachapoly-libcrypto.c \
        ssh-ed25519.c digest-openssl.c digest-libc.c \
        hmac.c ed25519.c hash.c \
        kex.c kex-names.c kexdh.c kexgex.c kexecdh.c kexc25519.c \
        kexgexc.c kexgexs.c \
        kexsntrup761x25519.c sntrup761.c kexgen.c \
        sftp-realpath.c platform-pledge.c platform-tracing.c platform-misc.c \
        sshbuf-io.c \
	\
	ssh-sk-client.c

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
CFLAGS+=	-I${.CURDIR}/../libldns
DPADD=		${LIBCRYPTO} ${LIBLDNS} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_crypto -lprivate_ldns -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.lib.mk>
