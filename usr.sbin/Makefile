#	From: @(#)Makefile	5.20 (Berkeley) 6/12/93
# $FreeBSD: src/usr.sbin/Makefile,v 1.183.2.14 2003/04/16 11:01:51 ru Exp $
# $DragonFly: src/usr.sbin/Makefile,v 1.28 2006/04/01 02:55:36 sephe Exp $

# XXX MISSING:		mkproto
SUBDIR=	IPXrouted \
	ac \
	accton \
	acpi \
	acxcontrol \
	adduser \
	amd \
	ancontrol \
	arp \
	asf \
	atm \
	authpf \
	battd \
	bootparamd \
	burncd \
	cdcontrol \
	chkgrp \
	chown \
	chroot \
	ckdist \
	clog \
	config \
	cron \
	crunch \
	daemon \
	dconschat \
	dev_mkdb \
	diskpart \
	dntpd \
	edquota \
	faithd \
	fdcontrol \
	fdformat \
	fdwrite \
	fwcontrol \
	gifconfig \
	ifmcstat \
	inetd \
	iostat \
	ipwcontrol \
	iwicontrol \
	jail \
	jexec \
	jls \
	kbdcontrol \
	kbdmap \
	kernbb \
	keyserv \
	lastlogin \
	mailwrapper \
	makewhatis \
	manctl \
	memcontrol \
	mergemaster \
	mixer \
	mld6query \
	mlxcontrol \
	moused \
	mrouted \
	mtest \
	mtree \
	ndiscvt \
	ndp \
	newsyslog \
	ngctl \
	nghook \
	ntpd \
	pciconf \
	periodic \
	pfctl \
	pflogd \
	pnpinfo \
	portmap \
	ppp \
	pppctl \
	pppd \
	pppstats \
	prefix \
	procctl \
	pstat \
	pw \
	pwd_mkdb \
	quot \
	quotaon \
	rarpd \
	raycontrol \
	rdate \
	repquota \
	resident \
	rip6query \
	rmt \
	route6d \
	rpc.lockd \
	rpc.statd \
	rpc.umntall \
	rpc.yppasswdd \
	rpc.ypupdated \
	rpc.ypxfrd \
	rrenumd \
	rtadvd \
	rtprio \
	rtsold \
	rwhod \
	sa \
	setkey \
	sliplogin \
	slstat \
	spray \
	syslogd \
	tcpdchk \
	tcpdmatch \
	tcpdump \
	timed \
	traceroute \
	traceroute6 \
	trpt \
	tzsetup \
	usbd \
	usbdevs \
	vidcontrol \
	vipw \
	vnconfig \
	watch \
	xten \
	yp_mkdb \
	ypbind \
	yppoll \
	yppush \
	ypserv \
	ypset \
	zic

.if !defined(NO_PKGTOOLS)
SUBDIR+=pkg_install
.endif

.if !defined(NO_IPFILTER)
SUBDIR+=ipftest \
	ipresend \
	ipsend \
	iptest
.endif

.if !defined(NO_BIND)
SUBDIR+=named \
	named.reload \
	named.restart \
	named-checkconf \
	named-checkzone \
	rndc \
	rndc-confgen \
	nslookup \
	nsupdate
.endif

.if !defined(NO_DHCPD)
SUBDIR+=dhcp
.endif

.if !defined(NO_LPR)
SUBDIR+=lpr
.endif

.if !defined(NO_SENDMAIL)
SUBDIR+=editmap \
	mailstats \
	makemap \
	praliases \
	sendmail
.endif

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=apm \
	apmd \
	btxld \
	kgmon \
	kgzip \
	lptcontrol \
	mptable \
	rndcontrol \
	sgsc \
	sicontrol \
	spkrtest \
	stallion \
	wicontrol \
	wlconfig
.if !defined(NO_I4B)
SUBDIR+=i4b
.endif
.endif

.if ${MACHINE} == "i386"
SUBDIR+=boot0cfg
.endif

.include <bsd.subdir.mk>
