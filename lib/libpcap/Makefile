# Makefile for libpcap
# $FreeBSD: src/lib/libpcap/Makefile,v 1.24.2.4 2002/07/22 14:21:50 ru Exp $
# $DragonFly: src/lib/libpcap/Makefile,v 1.5 2004/07/21 16:13:42 drhodus Exp $

LIB=    pcap
SRCS=	grammar.y tokdefs.h pcap-bpf.c version.h \
	pcap.c inet.c fad-getad.c gencode.c optimize.c nametoaddr.c \
	etherent.c savefile.c bpf_filter.c bpf_image.c bpf_dump.c \
	scanner.l version.c
INCS=	pcap.h pcap-namedb.h pcap-bpf.h pcap-int.h
MAN=	pcap.3
CLEANFILES=tokdefs.h version.c version.h

CFLAGS+=-DHAVE_CONFIG_H -Dyylval=pcap_lval -I${.CURDIR} -I.
CFLAGS+=-D_U_="__attribute__((unused))"
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=2
SHLIB_MINOR=5

#
# Magic to grab sources out of src/contrib
#
PCAP_DISTDIR?=${.CURDIR}/../../contrib/libpcap-0.8.3
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
	ln -sf grammar.h tokdefs.h

.include <bsd.lib.mk>
