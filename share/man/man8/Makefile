#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/share/man/man8/Makefile,v 1.13.2.12 2002/07/16 14:05:30 ru Exp $

MAN=	crash.8 \
	dhcp.8 \
	diskless.8 \
	intro.8 \
	rc.8 \
	rc.sendmail.8 \
	rc.subr.8 \
	sticky.8 \
	swapcache.8 \
	${_uefi.8} \
	yp.8

MLINKS=	rc.8 rc.d.8 \
	rc.8 rc.firewall.8 \
	rc.8 rc.local.8 \
	rc.8 rc.shutdown.8 \
	rc.8 rc.shutdown.local.8
MLINKS+=yp.8 nis.8 \
	yp.8 NIS.8 \
	yp.8 YP.8

.if ${MACHINE_ARCH} == "x86_64"
_uefi.8= uefi.8
.endif

.include <bsd.prog.mk>
