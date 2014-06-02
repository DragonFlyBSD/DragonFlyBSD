#	From: @(#)Makefile	5.20 (Berkeley) 6/12/93
# $FreeBSD: src/usr.sbin/Makefile,v 1.183.2.14 2003/04/16 11:01:51 ru Exp $

.include "../sys/platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR= 802_11 \
	ac \
	accton \
	acpi \
	adduser \
	amd \
	ancontrol \
	arp \
	asf \
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
	powerd \
	ppp \
	pppctl \
	pppd \
	pppstats \
	prefix \
	procctl \
	pstat \
	puffs \
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
	service \
	setkey \
	sliplogin \
	slstat \
	spray \
	syslogd \
	sysvipcd \
	tcpdchk \
	tcpdmatch \
	tcpdump \
	timed \
	traceroute \
	traceroute6 \
	trpt \
	tzsetup \
	${_usbconfig} \
	${_usbd} \
	${_usbdevs} \
	${_usbdump} \
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

.if defined(WANT_OLDUSB)
_usbd=		usbd
_usbdevs=	usbdevs
.else
_usbconfig=	usbconfig
_usbdump=	usbdump
.endif

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
	lptcontrol \
	mptable \
	rndcontrol \
	sicontrol
.endif

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=kgzip \
	pnpinfo \
	stlstats
.endif

.ifdef BOOT0CFG_SUPPORT
SUBDIR+=boot0cfg
.endif

.ifdef WANT_INSTALLER
SUBDIR+=installer
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
