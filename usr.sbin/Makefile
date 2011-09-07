#	From: @(#)Makefile	5.20 (Berkeley) 6/12/93
# $FreeBSD: src/usr.sbin/Makefile,v 1.183.2.14 2003/04/16 11:01:51 ru Exp $

.include "../sys/platform/${MACHINE_PLATFORM}/Makefile.inc"

# XXX MISSING:		mkproto
SUBDIR= 802_11 \
	IPXrouted \
	ac \
	accton \
	acpi \
	adduser \
	amd \
	ancontrol \
	arp \
	asf \
	atm \
	authpf \
	bootparamd \
	btconfig \
	bthcid \
	burncd \
	cdcontrol \
	chkgrp \
	chown \
	chroot \
	ckdist \
	clog \
	config \
	crashinfo \
	cron \
	daemon \
	dconschat \
	devinfo \
	dev_mkdb \
	dntpd \
	edquota \
	faithd \
	fdcontrol \
	fdformat \
	fdwrite \
	ftp-proxy \
	fwcontrol \
	gifconfig \
	hotplugd \
	ifmcstat \
	inetd \
	iostat \
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
	mfiutil \
	mixer \
	mld6query \
	mlxcontrol \
	moused \
	mptutil \
	mrouted \
	mtest \
	mtree \
	ndiscvt \
	ndp \
	newsyslog \
	ngctl \
	nghook \
	pciconf \
	periodic \
	pfctl \
	pflogd \
	pnpinfo \
	powerd \
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
	rpcbind \
	rrenumd \
	rtadvd \
	rtprio \
	rtsold \
	rwhod \
	sa \
	sdpd \
	sensorsd \
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
	vknetd \
	vnconfig \
	watch \
	watchdogd \
	wlandebug \
	yp_mkdb \
	ypbind \
	yppoll \
	yppush \
	ypserv \
	ypset \
	zic

.if !defined(NO_LPR)
SUBDIR+=lpr
.endif

.if !defined(NO_NS_CACHING)
SUBDIR+=nscd
.endif

.if !defined(NO_SENDMAIL)
SUBDIR+=editmap \
	mailstats \
	makemap \
	praliases \
	sendmail
.endif

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
SUBDIR+=apm \
	apmd \
	battd \
	btxld \
	kgmon \
	kgzip \
	lptcontrol \
	mptable \
	rndcontrol \
	sicontrol \
	spkrtest
.endif

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=stallion

.if !defined(NO_I4B)
SUBDIR+=i4b
.endif
.endif

.ifdef BOOT0CFG_SUPPORT
SUBDIR+=boot0cfg
.endif

.ifdef WANT_INSTALLER
SUBDIR+=installer
.endif

.include <bsd.subdir.mk>
