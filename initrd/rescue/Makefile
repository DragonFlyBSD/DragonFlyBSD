# $FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

PROG=		rescue

BINDIR=		/rescue

SCRIPTS=	dowpa \
		${.CURDIR}/../../sbin/dhclient/dhclient-script

# Programs from 'bin'
#
CRUNCH_SRCDIRS+=	bin
CRUNCH_PATH_bin=	${.CURDIR}/../..
CRUNCH_PROGS_bin=	\
	cat		\
	chmod		\
	cp		\
	cpdup		\
	date		\
	dd		\
	df		\
	echo		\
	ed		\
	expr		\
	hostname	\
	kill		\
	ln		\
	ls		\
	mined		\
	mkdir		\
	mv		\
	pax		\
	ps		\
	pwd		\
	realpath	\
	rm		\
	rmdir		\
	sh		\
	sleep		\
	stty		\
	sync		\
	test		\
	varsym

# Additional options for specific programs
CRUNCH_ALIAS_test=	[
CRUNCH_ALIAS_sh=	-sh
CRUNCH_SUPPRESS_LINK_-sh= YES # The -sh alias shouldn't appear as a link
CRUNCH_ALIAS_ed=	red
CRUNCH_ALIAS_ln=	link
CRUNCH_ALIAS_rm=	unlink

# Programs from 'usr.bin'
#
# Ignore diff3: a shell script that invokes /libexec/diff3prog
CRUNCH_SRCDIRS+=	usr.bin
CRUNCH_PATH_usr.bin=	${.CURDIR}/../..
CRUNCH_PROGS_usr.bin=	\
	awk		\
	basename	\
	bc		\
	bzip2		\
	cap_mkdb	\
	chflags		\
	cmp		\
	cpio		\
	cut		\
	diff		\
	dirname		\
	du		\
	env		\
	false		\
	fetch		\
	find		\
	gzip		\
	head		\
	hexdump		\
	kcollect	\
	kdump		\
	kenv		\
	ktrace		\
	id		\
	join		\
	less		\
	lsvfs		\
	nc		\
	paste		\
	patch		\
	pkill		\
	printf		\
	relpath		\
	scp		\
	sdiff		\
	sed		\
	sort		\
	ssh		\
	stat		\
	tail		\
	tar		\
	tee		\
	telnet		\
	touch		\
	tput		\
	tr		\
	true		\
	truss		\
	uname		\
	undo		\
	uniq		\
	vi		\
	wall		\
	wc		\
	xz		\
	yes

CRUNCH_SRCDIR_awk=	${.CURDIR}/../../usr.bin/awk/awk
CRUNCH_HOSTPROGS_awk=	${.CURDIR}/../../usr.bin/awk/maketab

CRUNCH_ALIAS_bzip2=	bunzip2 bzcat
CRUNCH_ALIAS_gzip=	gunzip gzcat zcat
CRUNCH_ALIAS_hexdump=	hd od
CRUNCH_ALIAS_id=	groups whoami
CRUNCH_ALIAS_less=	more
CRUNCH_ALIAS_pkill=	pgrep
CRUNCH_ALIAS_ssh=	slogin
CRUNCH_ALIAS_stat=	readlink
CRUNCH_ALIAS_vi=	ex nex nvi nview view
CRUNCH_ALIAS_xz=	lzcat lzma unlzma unxz xzcat

# Programs from 'sbin'
#
CRUNCH_SRCDIRS+=	sbin
CRUNCH_PATH_sbin=	${.CURDIR}/../..
CRUNCH_PROGS_sbin=	\
	camcontrol	\
	ccdconfig	\
	dhclient	\
	diskinfo	\
	disklabel32	\
	disklabel64	\
	dmesg		\
	dump		\
	dumpfs		\
	dumpon		\
	fdisk		\
	ffsinfo		\
	fsck		\
	fsck_hammer2	\
	fsck_msdosfs	\
	gpt		\
	growfs		\
	hammer		\
	hammer2		\
	ifconfig	\
	kldconfig	\
	kldload		\
	kldstat		\
	kldunload	\
	ldconfig	\
	md5		\
	mount		\
	mount_cd9660	\
	mount_devfs	\
	mount_hammer	\
	mount_hammer2	\
	mount_msdos	\
	mount_nfs	\
	mount_ntfs	\
	mount_null	\
	mount_tmpfs	\
	mount_ufs	\
	natacontrol	\
	newfs		\
	newfs_hammer	\
	newfs_hammer2	\
	newfs_msdos	\
	ping		\
	ping6		\
	rconfig		\
	rcorder		\
	reboot		\
	restore		\
	route		\
	savecore	\
	shutdown	\
	swapon		\
	sysctl		\
	tcplay		\
	tunefs		\
	udevd		\
	umount		\
	vinum

CRUNCH_ALIAS_disklabel64=	disklabel
CRUNCH_ALIAS_dump=	rdump
CRUNCH_ALIAS_md5=	rmd160 sha1 sha256 sha512
CRUNCH_ALIAS_newfs=	mount_mfs
CRUNCH_ALIAS_reboot=	halt
CRUNCH_ALIAS_restore=	rrestore
CRUNCH_ALIAS_shutdown=	poweroff
CRUNCH_ALIAS_swapon=	swapoff swapctl

# Programs from 'usr.sbin'
#
CRUNCH_SRCDIRS+=	usr.sbin
CRUNCH_PATH_usr.sbin=	${.CURDIR}/../..
CRUNCH_PROGS_usr.sbin=	\
	boot0cfg	\
	chown		\
	chroot		\
	fstyp		\
	pw		\
	pwd_mkdb	\
	rtsold		\
	traceroute	\
	traceroute6	\
	vnconfig	\
	vipw

CRUNCH_ALIAS_chown=	chgrp
CRUNCH_ALIAS_rtsold=	rtsol

# Programs from 'usr.sbin/802_11'
#
CRUNCH_SRCDIRS+=		usr.sbin/802_11
CRUNCH_PATH_usr.sbin/802_11=	${.CURDIR}/../..
CRUNCH_PROGS_usr.sbin/802_11=	wpa_supplicant

# Programs from 'gnu/sbin'
#
CRUNCH_SRCDIRS+=	gnu/sbin
CRUNCH_PATH_gnu/sbin=	${.CURDIR}/../..
CRUNCH_PROGS_gnu/sbin=	\
	cryptsetup	\
	dmsetup		\
	lvm

CRUNCH_ALIAS_lvm= \
	lvchange lvconvert lvcreate lvdisplay lvextend \
	lvmchange lvmdiskscan lvmdump \
	lvreduce lvremove lvrename lvresize lvs lvscan \
	pvchange pvck pvcreate pvdisplay pvmove pvremove \
	pvresize pvs pvscan \
	vgcfgbackup vgcfgrestore vgchange vgck vgconvert \
	vgcreate vgdisplay vgexport vgextend vgimport \
	vgmerge vgmknodes vgreduce vgremove vgrename \
	vgs vgscan vgsplit

# Programs from 'gnu/usr.bin'
#
CRUNCH_SRCDIRS+=	grep
CRUNCH_PATH_grep=	${.CURDIR}/../../gnu/usr.bin
CRUNCH_PROGS_grep=	grep
SCRIPTS+= \
	${CRUNCH_PATH_grep}/grep/egrep/egrep \
	${CRUNCH_PATH_grep}/grep/fgrep/fgrep

# Per-program internal libraries
#
CRUNCH_INTLIB_grep=		libgreputils.a
CRUNCH_SRCDIR_libgreputils.a=	${CRUNCH_PATH_grep}/grep/libgreputils
CRUNCH_INTLIB_telnet=		libtelnet.a
CRUNCH_SRCDIR_libtelnet.a=	${.CURDIR}/../../lib/libtelnet

# Per-program system libraries
#
# NOTE: Separately link ${LIBDM} for cryptsetup/tcplay to avoid symbols
# being hidden by libdevmapper.
#
# NOTE: Separately link ${LIBSSH} for scp/ssh to avoid the tilde_expand()
# symbol conflict with libprivate_edit.a.
#
CRUNCH_LIB_cryptsetup=		${LIBDM}
CRUNCH_LIB_tcplay=		${LIBDM}
CRUNCH_LIB_scp=			${LIBSSH}
CRUNCH_LIB_ssh=			${LIBSSH} ${LIBLDNS}
CRUNCH_LIB_camcontrol=		${LIBCAM}
CRUNCH_LIB_fetch=		${LIBFETCH}
CRUNCH_LIB_hammer=		${LIBHAMMER}
CRUNCH_LIB_hammer2=		${LIBDMSG}
CRUNCH_LIB_ifconfig=		${LIBBSDXML}
CRUNCH_LIB_lvm=			${LIBLVM} ${LIBDEVATTR}
CRUNCH_LIB_vinum=		${LIBDEVSTAT}
CRUNCH_LIB_wpa_supplicant=	${LIBPCAP}

# System libraries
#
CRUNCH_LIBS+= -ldevmapper  # dmsetup, lvm
CRUNCH_LIBS+= -lprop  # cryptsetup, dmsetup, lvm, tcplay
CRUNCH_LIBS+= -lsbuf  # camcontrol, ifconfig
CRUNCH_LIBS+= -lkiconv  # mount_{cd9660,msdos,ntfs}
CRUNCH_LIBS+= -larchive  # cpio, tar
CRUNCH_LIBS+= -llzma  # cpio, tar, xz
CRUNCH_LIBS+= -lbz2  # bzip2, cpio, tar
CRUNCH_LIBS+= -lkvm  # ccdconfig, dmesg, pkill, ps
CRUNCH_LIBS+= -lcrypt  # pw, telnet
CRUNCH_LIBS+= -lprivate_ssl  # fetch, wpa_supplicant
CRUNCH_LIBS+= -lprivate_crypto # cpio, hammer2, md5, ping6, tar
CRUNCH_LIBS+= -lprivate_edit  # bc, sh, vinum
#CRUNCH_LIBS+= -lprivate_ncurses  # bc, less, ls, telnet, tput, vinum (=> ncursesw)
CRUNCH_LIBS+= -lprivate_ncursesw  # vi
CRUNCH_LIBS+= -lprivate_zstd
CRUNCH_LIBS+= -lutil
CRUNCH_LIBS+= -lz  # cpio, savecore, ssh, tar
CRUNCH_LIBS+= -lm  # awk, hammer, hammer2, ping, ping6, ps

CRUNCH_LINKOPTS+= ${PRIVATELIB_LDFLAGS}

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
