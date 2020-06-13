#	From: @(#)Makefile	5.20 (Berkeley) 6/12/93
# $FreeBSD: src/usr.sbin/Makefile,v 1.183.2.14 2003/04/16 11:01:51 ru Exp $

.include "../sys/platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR= 802_11 \
	ac \
	accton \
	acpi \
	adduser \
	arp \
	asf \
	authpf \
	autofs \
	boot0cfg \
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
	cpucontrol \
	crashinfo \
	cron \
	daemon \
	dconschat \
	devinfo \
	dev_mkdb \
	dntpd \
	edquota \
	efibootmgr \
	efidp \
	efisetup \
	efivar \
	fdcontrol \
	fdformat \
	fdwrite \
	fstyp \
	ftp-proxy \
	fwcontrol \
	gifconfig \
	hotplugd \
	ifmcstat \
	inetd \
	iostat \
	ip6addrctl \
	jail \
	jexec \
	jls \
	kbdcontrol \
	kbdmap \
	keyserv \
	lastlogin \
	mailwrapper \
	makefs \
	manctl \
	memcontrol \
	mergemaster \
	mfiutil \
	mixer \
	mld6query \
	mlxcontrol \
	moused \
	mpsutil \
	mptutil \
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
	prefix \
	procctl \
	pstat \
	pw \
	pwd_mkdb \
	quot \
	quotaon \
	rarpd \
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
	rtadvctl \
	rtprio \
	rtsold \
	rwhod \
	sa \
	sdpd \
	sensorsd \
	service \
	sshd \
	sshlockout \
	sliplogin \
	slstat \
	spray \
	syslogd \
	sysvipcd \
	tcpdchk \
	tcpdmatch \
	tcpdrop \
	tcpdump \
	traceroute \
	traceroute6 \
	trpt \
	tzsetup \
	uefisign \
	usbconfig \
	usbdump \
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

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+= \
	lptcontrol \
	mptable \
	rndcontrol
.endif

.ifdef WANT_INSTALLER
SUBDIR+=installer
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
