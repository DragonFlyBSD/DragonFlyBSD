# From: Id: Makefile.inc,v 8.4 1996/03/03 17:42:43 vixie Exp
# $FreeBSD: src/usr.sbin/named/Makefile.inc,v 1.12.2.3 2001/07/22 18:57:06 dillon Exp $
# $DragonFly: src/usr.sbin/named/Makefile.inc,v 1.4 2004/05/27 18:15:42 dillon Exp $

.ifndef (Mk.Inc)
Mk.Inc?=defined

BIND_DIR?=	${.CURDIR}/../../contrib/bind-9.2.4rc4

VER=		9.2.4rc4

PS=		ps
PIDDIR=		/var/run
DESTETC=	/etc/namedb
DESTEXEC=	/usr/libexec
DESTRUN=	/var/run
DESTSBIN=	/usr/sbin
DESTHELP=	/usr/share/misc

# note: lib/bind/include not included here because we may want to
# use the system resolv.h and friends instead of the one in libbind.
#
CFLAGS+=	-I${BIND_DIR}/port/freebsd/include \
		-I${BIND_DIR}/lib/dns/include \
		-I${BIND_DIR}/lib/isc/nothreads/include \
		-I${BIND_DIR}/lib/isc/unix/include \
		-I${BIND_DIR}/lib/isc/include \
		-I${BIND_DIR}/lib/isccc/include \
		-I${BIND_DIR}/lib/lwres/unix/include \
		-I${BIND_DIR}/lib/lwres/include \
		-I${BIND_DIR}/lib/isccfg/include \
		-I${BIND_DIR}/lib/dns/sec/dst/include \
		-I${BIND_DIR}/lib/dns

CFLAGS+=	-I. -I${.CURDIR}

CFLAGS+=	-DVERSION=\"${VER}\" \
		-DNS_LOCALSTATEDIR=\"/var\" \
		-DNS_SYSCONFDIR=\"${DESTETC}\"

# This is for dig and other utilities
#
.if defined(USE_LIBBIND)

.if exists(${.OBJDIR}/../../lib/libbind)
LIBBINDDIR:=	${.OBJDIR}/../../lib/libbind
.else
LIBBINDDIR!=	cd ${.CURDIR}/../../lib/libbind; make -V .OBJDIR
.endif
LIBBIND:=	${LIBBINDDIR}/libbind.a

DPADD+=		${LIBBIND}
LDADD+=		${LIBBIND}

CLEANFILES+=

.endif

.if defined(USE_LIBISC)

.if exists(${.OBJDIR}/../../lib/libisc)
LIBISCDIR:=	${.OBJDIR}/../../lib/libisc
.else
LIBISCDIR!=	cd ${.CURDIR}/../../lib/libisc; make -V .OBJDIR
.endif
LIBISC:=	${LIBISCDIR}/libisc.a

DPADD+=		${LIBISC}
LDADD+=		${LIBISC}

CLEANFILES+=

.endif

.include	"Makefile.maninc"
.endif
