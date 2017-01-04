#	@(#)Makefile	8.5 (Berkeley) 3/31/94
# $FreeBSD: src/sbin/Makefile,v 1.77.2.9 2002/08/08 09:03:46 ru Exp $
#
# XXX MISSING:		icheck ncheck

SUBDIR=	adjkerntz \
	badsect \
	camcontrol \
	ccdconfig \
	clri \
	comcontrol \
	devattr \
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
	fsid \
	fsirand \
	getdevpath \
	growfs \
	gpt \
	hammer \
	${_hammer2} \
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
	mkinitrd \
	mountctl \
	mount \
	mount_ufs \
	mount_cd9660 \
	mount_devfs \
	mount_dirfs \
	mount_ext2fs \
	mount_hammer \
	${_mount_hammer2} \
	mount_hpfs \
	mount_msdos \
	mount_nfs \
	mount_ntfs \
	mount_null \
	mount_std \
	mount_tmpfs \
	mount_udf \
	mountd \
	natacontrol \
	natd \
	newbtconf \
	newfs \
	newfs_hammer \
	${_newfs_hammer2} \
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

.if !defined(NO_CRYPT)
SUBDIR+=cryptdisks \
	tcplay

# include hammer2 utilities (requires LibreSSL)
#
.  if defined(WANT_HAMMER2)
_mount_hammer2= mount_hammer2
_newfs_hammer2=	newfs_hammer2
_hammer2=	hammer2
.  endif
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
