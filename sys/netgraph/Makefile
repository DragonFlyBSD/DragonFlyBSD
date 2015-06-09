# $Whistle: Makefile,v 1.5 1999/01/24 06:48:37 archie Exp $
# $FreeBSD: src/sys/modules/netgraph/Makefile,v 1.8.2.8 2002/08/20 23:48:15 archie Exp $

SUBDIR=	UI \
	async \
	bpf \
	bridge \
	cisco \
	echo \
	eiface \
	etf \
	ether \
	fec \
	frame_relay \
	hole \
	iface \
	ksocket \
	l2tp \
	lmi \
	netgraph \
	one2many \
	ppp \
	pppoe \
	pptpgre \
	rfc1490 \
	socket \
	tee \
	tty \
	vjc

.if !defined(NO_CRYPT) && exists(${.CURDIR}/../crypto/rc4/rc4.c)
SUBDIR+=	mppc
.endif

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=	sync_ar sync_sr
.endif

# empty to allow concurrent building
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
