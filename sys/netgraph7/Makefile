# $Whistle: Makefile,v 1.5 1999/01/24 06:48:37 archie Exp $
# $FreeBSD: src/sys/modules/netgraph/Makefile,v 1.8.2.8 2002/08/20 23:48:15 archie Exp $
# $DragonFly$

SUBDIR=	UI \
	async \
	atmllc \
	bpf \
	bridge \
	cisco \
	deflate \
	echo \
	ether \
	hole \
	iface \
	ksocket \
	netgraph \
	pptpgre \
	socket \
	tee \
	vjc
#	eiface \
#	etf \
#	fec \
#	frame_relay \
#	l2tp \
#	lmi \
#	netgraph \
#	one2many \
#	ppp \
#	pppoe \
#	rfc1490 \
#	socket \
#	sync_ar \
#	sync_sr \
#	tty \

.if !defined(NO_CRYPT) && exists(${.CURDIR}/../crypto/rc4/rc4.c)
#SUBDIR+=	mppc
.endif

.include <bsd.subdir.mk>
