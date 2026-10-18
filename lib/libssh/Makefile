.include "Makefile.common"

LIB=		ssh
INTERNALLIB=	YES

# From vendor's Makefile
LIBOPENSSH_OBJS=\
	ssh_api.o \
	ssherr.o \
	sshbuf.o \
	sshkey.o \
	sshbuf-getput-basic.o \
	sshbuf-misc.o \
	sshbuf-getput-crypto.o \
	krl.o \
	bitmap.o
LIBSSH_OBJS=${LIBOPENSSH_OBJS} \
	authfd.o authfile.o \
	canohost.o channels.o cipher.o cipher-aes.o cipher-aesctr.o \
	cleanup.o \
	compat.o fatal.o hostfile.o \
	log.o match.o moduli.o nchan.o packet.o \
	readpass.o ttymodes.o xmalloc.o addr.o addrmatch.o \
	atomicio.o dispatch.o mac.o misc.o utf8.o \
	monitor_fdpass.o rijndael.o ssh-ecdsa.o ssh-ecdsa-sk.o \
	ssh-ed25519-sk.o ssh-rsa.o dh.o \
	msg.o dns.o entropy.o gss-genr.o umac.o umac128.o \
	smult_curve25519_ref.o \
	poly1305.o chacha.o cipher-chachapoly.o cipher-chachapoly-libcrypto.o \
	ssh-ed25519.o digest-openssl.o digest-libc.o \
	libcrux-mlkem-mldsa.o ssh-mldsa-eddsa.o \
	hmac.o ed25519.o ed25519-openssl.o \
	kex.o kex-names.o kexdh.o kexgex.o kexecdh.o kexc25519.o \
	kexgexc.o kexgexs.o kexmlkem768ecdh.o \
	kexsntrup761x25519.o kexmlkem768x25519.o sntrup761.o kexgen.o \
	sftp-realpath.o platform-pledge.o platform-tracing.o platform-misc.o \
	sshbuf-io.o misc-agent.o ssherr-libcrypto.o

# Necessary sources from the portability layer (openbsd-compat)
.PATH: ${OPENSSHDIR}/openbsd-compat
COMPAT_SRCS= \
	bcrypt_pbkdf.c blowfish.c bsd-misc.c fmt_scaled.c \
	getrrsetbyname-ldns.c glob.c mktemp.c openssl-compat.c \
	port-net.c xcrypt.c

SRCS=	${LIBSSH_OBJS:.o=.c}
SRCS+=	${COMPAT_SRCS}

WARNS?=	2
NO_STRICT_ALIASING=

CFLAGS+=	-DTAILQ_FOREACH_SAFE=TAILQ_FOREACH_MUTABLE

FLAGS_GROUPS=		warn
warn_FLAGS=		-Wno-discarded-qualifiers
warn_FLAGS_FILES=	misc.c

# NOTE: The 'configure' script misdetects a few things, so apply the following
# corrections.  Although some of the changes are unnecessary, keep them to be
# more future-proof.
#
# - HAVE_CRYPT: ensure xcrypt.c correctly choose crypt() other than DES_crypt()
# - SSH_TUN_FREEBSD, SSH_TUN_COMPAT_AF, HAVE_NET_IF_TUN_H: make port-net.c work
# - LOCKED_PASSWD_PREFIX: account lock check
# - DISABLE_LASTLOG: undefine to enable last login record handling
# - UTXDB_LASTLOGIN: compat with FreeBSD's last login database
#
XAUTH_PATH?=	/usr/local/bin/xauth
CONFIGSUBS=	\
	-e 's@^.*[[:space:]]+XAUTH_PATH[[:space:]]+.*$$@\#define XAUTH_PATH "${XAUTH_PATH}"@' \
	-e 's@^.*[[:space:]]+HAVE_CRYPT[[:space:]]+.*$$@\#define HAVE_CRYPT 1@' \
	-e 's@^.*[[:space:]]+LOCKED_PASSWD_PREFIX[[:space:]]+.*$$@\#define LOCKED_PASSWD_PREFIX "*LOCKED*"@' \
	-e 's@^.*[[:space:]]+SSH_TUN_FREEBSD[[:space:]]+.*$$@\#define SSH_TUN_FREEBSD 1@' \
	-e 's@^.*[[:space:]]+SSH_TUN_COMPAT_AF[[:space:]]+.*$$@\#define SSH_TUN_COMPAT_AF 1@' \
	-e 's@^.*[[:space:]]+HAVE_NET_IF_TUN_H[[:space:]]+.*$$@\#define HAVE_NET_IF_TUN_H 1@' \
	-e 's@^.*[[:space:]]+DISABLE_LASTLOG[[:space:]]+.*$$@/* \#undef DISABLE_LASTLOG */@' \
	-e '$$a\${.newline}\#define UTXDB_LASTLOGIN UTX_DB_LASTLOGX'

GENFILES+=	config.h
config.h: ${.CURDIR}/config.h.auto
	sed -E ${CONFIGSUBS} ${.ALLSRC} > ${.TARGET}

GENFILES+=	sys/tree.h glob.h netgroup.h
sys/tree.h:
	mkdir -p sys
	echo '#include "openbsd-compat/sys-tree.h"' > ${.TARGET}
glob.h:
	echo '#include "openbsd-compat/glob.h"' > ${.TARGET}
netgroup.h:
	touch ${.TARGET}

# XXX: FreeBSD compat for openbsd-compat/port-net.c
GENFILES+=	net/if_tun.h
net/if_tun.h:
	mkdir -p net
	echo '#include <net/tun/if_tun.h>' > ${.TARGET}

# Derived from vendor's Makefile
PATHSUBS=	\
	-e 's|/etc/shosts.equiv|/etc/ssh/shosts.equiv|g' \
	-e 's|/etc/moduli|/etc/ssh/moduli|g' \
	-e 's|/usr/X11R6/bin/xauth|$(XAUTH_PATH)|g'
MANPAGES_IN=	\
	moduli.5 scp.1 ssh-add.1 ssh-agent.1 ssh-keygen.1 ssh-keyscan.1 \
	ssh.1 sshd.8 sftp-server.8 sftp.1 ssh-keysign.8 ssh-pkcs11-helper.8 \
	ssh-sk-helper.8 sshd_config.5 ssh_config.5

GENFILES+=	${MANPAGES_IN}
.for F in ${MANPAGES_IN}
${F}: ${OPENSSHDIR}/${F}
	sed ${PATHSUBS} ${.ALLSRC} > ${.TARGET}
.endfor

CLEANFILES+=	${GENFILES}
CLEANDIRS+=	sys net

beforedepend: ${GENFILES}

.include <bsd.lib.mk>
