# $FreeBSD: src/lib/libbind/Makefile,v 1.5.2.2 2002/07/19 18:46:25 ru Exp $
# $DragonFly: src/lib/libbind/Makefile,v 1.9 2006/09/20 21:48:39 victor Exp $

BIND_DIR=${.CURDIR}/../../contrib/bind-9.3

# note: lib/bind/include/isc/list.h must come before lib/isc/include/isc/list.h
#
CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind/include

.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"
.include "${BIND_DIR}/lib/bind/api"

LIB= bind
WANT_IRS= for now

# This may or may not work yet.  It's not compatible with the core
# system components since it overrides the master.passwd handling etc.
.if defined(WANT_IRS)
.PATH:	${BIND_DIR}/lib/bind/irs

SRCS+=	dns.c dns_gr.c dns_ho.c dns_nw.c dns_pr.c dns_pw.c \
	dns_sv.c gai_strerror.c gen.c gen_gr.c gen_ho.c \
	gen_ng.c gen_nw.c gen_pr.c gen_pw.c gen_sv.c \
	getaddrinfo.c getgrent.c getgrent_r.c gethostent.c \
	gethostent_r.c getnameinfo.c getnetent.c \
	getnetent_r.c getnetgrent.c getnetgrent_r.c \
	getprotoent.c getprotoent_r.c getpwent.c \
	getpwent_r.c getservent.c getservent_r.c hesiod.c \
	irp.c irp_gr.c irp_ho.c irp_ng.c irp_nw.c irp_pr.c \
	irp_pw.c irp_sv.c irpmarshall.c irs_data.c lcl.c \
	lcl_gr.c lcl_ho.c lcl_ng.c lcl_nw.c lcl_pr.c\
	lcl_pw.c lcl_sv.c nis.c nis_gr.c nis_ho.c nis_ng.c \
	nis_nw.c nis_pr.c nis_pw.c nis_sv.c nul_ng.c util.c

# kind of a messy way to patch this file
#
CONTRIBDIR= ${BIND_DIR}/lib/bind/irs
CFLAGS+=-I${CONTRIBDIR}
SRCS+=	dns_ho.c.patch

.PATH:	${BIND_DIR}/lib/bind/nameser
SRCS+=	ns_date.c ns_name.c ns_netint.c \
	ns_parse.c ns_print.c  ns_samedomain.c \
	ns_sign.c ns_ttl.c ns_verify.c

.PATH:	${BIND_DIR}/lib/bind/resolv
SRCS+=	herror.c mtctxres.c res_comp.c res_data.c \
	res_debug.c res_findzonecut.c res_init.c \
	res_mkquery.c res_mkupdate.c res_query.c \
	res_send.c res_sendsigned.c res_update.c

.endif

.PATH:	${BIND_DIR}/lib/bind/dst
CFLAGS+=-DHMAC_MD5 -DUSE_MD5
SRCS+=	dst_api.c hmac_link.c md5_dgst.c support.c

.PATH:	${BIND_DIR}/lib/bind/isc
SRCS+=	assertions.c base64.c bitncmp.c ctl_clnt.c \
	ctl_p.c ctl_srvr.c ev_connects.c ev_files.c \
	ev_streams.c ev_timers.c ev_waits.c \
	eventlib.c heap.c hex.c logging.c \
	memcluster.c movefile.c tree.c


INTERNALLIB=		true

.include <bsd.lib.mk>

