# Makefile for libpcap
# $FreeBSD: src/lib/libpcap/Makefile,v 1.24.2.4 2002/07/22 14:21:50 ru Exp $
# $DragonFly: src/lib/libpcap/Makefile,v 1.10 2007/03/16 12:59:10 sephe Exp $

SRCDIR=	${.CURDIR}/../../contrib/libpcap-0.9
CONTRIBDIR=${SRCDIR}

PATCHES!= echo ${.CURDIR}/patches/*.patch

LIB=    pcap
SRCS=	grammar.y tokdefs.h pcap-bpf.c version.h \
	pcap.c inet.c fad-getad.c gencode.c optimize.c nametoaddr.c \
	etherent.c savefile.c bpf_filter.c bpf_image.c bpf_dump.c \
	scanner.l version.c
SRCS+=	${PATCHES}
INCS=	pcap.h pcap-namedb.h pcap-bpf.h pcap-int.h
MAN=	pcap.3
CLEANFILES=tokdefs.h version.c version.h

CFLAGS+=-DHAVE_CONFIG_H -Dyylval=pcap_lval -I${.CURDIR} -I.
CFLAGS+=-D_U_="__attribute__((unused))"
CFLAGS+=-DHAVE_STRUCT_ETHER_ADDR
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

#
# Magic to grab sources out of src/contrib
#
PCAP_DISTDIR?=${SRCDIR}
CFLAGS+=-I${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}/bpf/net

version.c: ${PCAP_DISTDIR}/VERSION
	@rm -f $@
	sed 's/.*/char pcap_version[] = "&";/' ${PCAP_DISTDIR}/VERSION > $@

version.h: ${PCAP_DISTDIR}/VERSION
	@rm -f $@
	sed 's/.*/static const char pcap_version_string[] = "libpcap version &";/' \
		${PCAP_DISTDIR}/VERSION > $@

tokdefs.h: grammar.h
	${LN} -sf grammar.h tokdefs.h

.include <bsd.lib.mk>
