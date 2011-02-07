#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/Makefile,v 1.42.2.5 2002/11/12 17:32:48 obrien Exp $
# $DragonFly: src/libexec/Makefile,v 1.13 2008/02/02 18:20:51 matthias Exp $

# Present but disabled: kpasswdd
SUBDIR=	atrun \
	bootpd \
	comsat \
	customcc \
	dma \
	fingerd \
	ftpd \
	getty \
	makekey \
	makewhatis.local \
	mknetid \
	pppoed \
	rbootd \
	revnetgroup \
	rlogind \
	rpc.rquotad \
	rpc.rstatd \
	rpc.rusersd \
	rpc.rwalld \
	rpc.sprayd \
	rshd \
	rtld-elf \
	talkd \
	tcpd \
	telnetd \
	tftpd \
	utmp_update \
	xtend \
	ypxfr

.if !defined(NO_SENDMAIL)
SUBDIR+=mail.local smrsh
.endif

.include <bsd.subdir.mk>
