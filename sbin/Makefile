#	@(#)Makefile	8.5 (Berkeley) 3/31/94
# $FreeBSD: src/sbin/Makefile,v 1.77.2.9 2002/08/08 09:03:46 ru Exp $

SUBDIR=	adjkerntz \
	badsect \
	camcontrol \
	ccdconfig \
	clri \
	comcontrol \
	cryptdisks \
	devattr \
	devd \
	devfsctl \
	dhclient \
	dhcpcd \
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
	fsid \
	fsirand \
	getdevpath \
	growfs \
	gpt \
	hammer \
	hammer2 \
	ifconfig \
	init \
	ip6fw \
	ipfw \
	ipfw3 \
	iscontrol \
	jscan \
	kldconfig \
	kldload \
	kldstat \
	kldunload \
	ldconfig \
	md5 \
	mountctl \
	mount \
	mount_ufs \
	mount_cd9660 \
	mount_devfs \
	mount_dirfs \
	mount_ext2fs \
	mount_hammer \
	mount_hammer2 \
	mount_hpfs \
	mount_msdos \
	mount_nfs \
	mount_ntfs \
	mount_null \
	mount_std \
	mount_tmpfs \
	mount_udf \
	mount_autofs \
	mount_fuse \
	mountd \
	natacontrol \
	natd \
	newbtconf \
	newfs \
	newfs_hammer \
	newfs_hammer2 \
	newfs_msdos \
	nfsd \
	nfsiod \
	nologin \
	nos-tun \
	nvmectl \
	ping \
	ping6 \
	quotacheck \
	reboot \
	rconfig \
	rcorder \
	resolvconf \
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
	svc \
	tcplay \
	tunefs \
	udevd \
	umount \
	usched \
	vinum \
	vquota

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=mount_smbfs \
	fdisk \
	nextboot
.endif

.if exists(${.CURDIR}/${MACHINE})
SUBDIR+= ${MACHINE}
.endif
.if exists(${.CURDIR}/${MACHINE_ARCH})
SUBDIR+= ${MACHINE_ARCH}
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
