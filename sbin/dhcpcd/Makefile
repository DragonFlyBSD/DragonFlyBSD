# Derived from NetBSD's Makefile for dhcpcd

DISTDIR=	${.CURDIR}/../../contrib/dhcpcd

PROG=		dhcpcd

SRCDIR=		${DISTDIR}/src
.PATH:		${SRCDIR}
CFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR} -I${SRCDIR} -I${DISTDIR}
SRCS=		common.c control.c dhcpcd.c duid.c eloop.c logerr.c
SRCS+=		if.c if-options.c route.c sa.c script.c
SRCS+=		dhcp-common.c dhcpcd-embedded.c
SRCS+=		if-bsd.c

CFLAGS+=	-DAUTH
SRCS+=		auth.c

CFLAGS+=	-DARP -DINET
SRCS+=		arp.c bpf.c dhcp.c ipv4.c
CFLAGS+=	-DIPV4LL -DARPING
SRCS+=		ipv4ll.c

CFLAGS+=	-DINET6
SRCS+=		ipv6.c ipv6nd.c
CFLAGS+=	-DDHCP6
SRCS+=		dhcp6.c

.PATH:		${DISTDIR}/compat
SRCS+=		pidfile.c strtoi.c strtou.c
.PATH:		${DISTDIR}/compat/crypt
SRCS+=		hmac.c

# Fully convert to openssl/md5.h and openssl/sha2.h
FLAGS_GROUPS=	openssl
openssl_FLAGS=	-I${_SHLIBDIRPREFIX}/usr/include/priv/openssl
openssl_FLAGS+=	-DMD5Init=MD5_Init -DMD5Update=MD5_Update -DMD5Final=MD5_Final
openssl_FLAGS_FILES= hmac.c ipv6.c

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBCRYPTO}
LDADD=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

# system directories
_SYSCONFDIR=	/etc
_LIBDIR=	/usr/lib
_LIBEXECDIR=	/usr/libexec
_DBDIR=		/var/db/dhcpcd
_RUNDIR=	/var/run
CFLAGS+=	-DSYSCONFDIR='"${_SYSCONFDIR}"' \
		-DSBINDIR='"${BINDIR}"' \
		-DLIBDIR='"${_LIBDIR}"' \
		-DLIBEXECDIR='"${_LIBEXECDIR}"' \
		-DDBDIR='"${_DBDIR}"' \
		-DRUNDIR='"${_RUNDIR}"'

_DATADIR=	/usr/share/examples
_HOOKDIR=	${_LIBEXECDIR}/dhcpcd-hooks
_SED_CMDS=	-e 's:@SYSCONFDIR@:${_SYSCONFDIR}:g' \
		-e 's:@DATADIR@:${_DATADIR}:g' \
		-e 's:@DBDIR@:${_DBDIR}:g' \
		-e 's:@LIBDIR@:${_LIBDIR}:g' \
		-e 's:@RUNDIR@:${_RUNDIR}:g' \
		-e 's:@HOOKDIR@:${_HOOKDIR}:g' \
		-e 's:@SCRIPT@:${_LIBEXECDIR}/dhcpcd-run-hooks:g' \
		-e 's:@SERVICEEXISTS@::g' \
		-e 's:@SERVICECMD@::g' \
		-e 's:@SERVICESTATUS@::g' \
		-e 's:@STATUSARG@::g' \
		-e 's:@YPDOMAIN_DIR@::g' \
		-e 's:@YPDOMAIN_SUFFIX@::g'

MAN=		dhcpcd.conf.5 dhcpcd.8
.for f in dhcpcd.conf.5 dhcpcd.8
CLEANFILES+=	${f}
${f}: ${f}.in
	sed ${_SED_CMDS} ${SRCDIR}/${f}.in > $@
.endfor

# XXX -Wunused-parameter in duid.c, not implemented duid_machineuuid().
WARNS?=		3

# NOTE:
# The configuration file 'dhcpcd.conf' is installed by 'etc/Makefile'

#
# Hooks
#

HOOKSRC=	${DISTDIR}/hooks
.PATH:		${HOOKSRC}

SCRIPTS=	dhcpcd-run-hooks
SCRIPTSDIR=	${_LIBEXECDIR}
MAN+=		dhcpcd-run-hooks.8

FILES=		01-test 02-dump 20-resolv.conf 30-hostname
FILESDIR=	${_HOOKDIR}

# Example hooks that should not be installed by default
.for f in 10-wpa_supplicant 15-timezone 29-lookup-hostname 50-ypbind
FILES+=		${f}
FILESDIR_${f}=	${_DATADIR}/dhcpcd/hooks
.endfor

.for f in dhcpcd-run-hooks dhcpcd-run-hooks.8 50-ypbind
CLEANFILES+=	${f}
${f}: ${f}.in
	sed ${_SED_CMDS} ${HOOKSRC}/${f}.in > $@
.endfor

.include <bsd.prog.mk>
