#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/Makefile,v 1.42.2.5 2002/11/12 17:32:48 obrien Exp $
# $DragonFly: src/libexec/Makefile,v 1.7 2004/03/25 18:05:48 joerg Exp $

# Present but disabled: kpasswdd
SUBDIR=	atrun \
	bootpd \
	comsat \
	fingerd \
	ftpd \
	getNAME \
	getty \
	makekey \
	makewhatis.local \
	mknetid \
	pppoed \
	rbootd \
	revnetgroup \
	rexecd \
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
	tftpd \
	xtend \
	ypxfr

.if !defined(NO_BIND)
SUBDIR+=named-xfer
.endif

.if !defined(NO_SENDMAIL)
SUBDIR+=mail.local smrsh
.endif

.if defined(RELEASEDIR) || \
    !exists(${.CURDIR}/../secure) || defined(NOCRYPT)
# make release needs both
SUBDIR+=telnetd
.endif

.if defined(WANT_LUKEMFTPD)
SUBDIR+=lukemftpd
.endif

.include <bsd.subdir.mk>
