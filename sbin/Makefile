#	@(#)Makefile	8.5 (Berkeley) 3/31/94
# $FreeBSD: src/sbin/Makefile,v 1.77.2.9 2002/08/08 09:03:46 ru Exp $
# $DragonFly: src/sbin/Makefile,v 1.20 2008/10/03 00:26:21 hasso Exp $
#
# XXX MISSING:		icheck ncheck

SUBDIR=	adjkerntz \
	atacontrol \
	atm \
	badsect \
	camcontrol \
	ccdconfig \
	clri \
	comcontrol \
	cryptdisks \
	devd \
	devfsctl \
	dhclient \
	disklabel32 \
	disklabel64 \
	diskinfo \
	dmesg \
	dump \
	dumpfs \
	dumpon \
	ffsinfo \
	fsck \
	fsck_msdosfs \
	fsdb \
	fsirand \
	getdevpath \
	growfs \
	gpt \
	hammer \
	ifconfig \
	init \
	ip6fw \
	ipfw \
	iscontrol \
	jscan \
	kldconfig \
	kldload \
	kldstat \
	kldunload \
	ldconfig \
	md5 \
	mkinitrd \
	mknod \
	mountctl \
	mount \
	mount_cd9660 \
	mount_devfs \
	mount_ext2fs \
	mount_hammer \
	mount_hpfs \
	mount_msdos \
	mount_nfs \
	mount_ntfs \
	mount_null \
	mount_portal \
	mount_std \
	mount_tmpfs \
	mount_udf \
	mount_union \
	mountd \
	natacontrol \
	natd \
	newbtconf \
	newfs \
	newfs_hammer \
	newfs_msdos \
	nfsd \
	nfsiod \
	nologin \
	nos-tun \
	ping \
	ping6 \
	quotacheck \
	reboot \
	rconfig \
	rcorder \
	restore \
	route \
	routed \
	rtsol \
	rcrun \
	savecore \
	shutdown \
	slattach \
	spppcontrol \
	startslip \
	swapon \
	sysctl \
	tunefs \
	udevd \
	umount \
	vinum

.if !defined(NO_IPFILTER)
SUBDIR+=ipf \
	ipfs \
	ipfstat \
	ipmon \
	ipnat
.endif

.if ${MACHINE_ARCH} == i386 || ${MACHINE_ARCH} == x86_64
.if ${MACHINE_ARCH} == i386
SUBDIR+=kget
.endif
SUBDIR+=mount_nwfs \
	mount_smbfs \
	fdisk \
	nextboot
.endif

.if exists(${.CURDIR}/${MACHINE})
SUBDIR+= ${MACHINE}
.endif
.if exists(${.CURDIR}/${MACHINE_ARCH})
SUBDIR+= ${MACHINE_ARCH}
.endif

.include <bsd.subdir.mk>
