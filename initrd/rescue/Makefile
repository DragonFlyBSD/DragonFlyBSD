# $FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

# To include '../Makefine.inc' for use in <bsd.crunchgen.mk>
.include <bsd.init.mk>

PROG= rescue

SCRIPTS= dowpa ${.CURDIR}/../../sbin/dhclient/dhclient-script

# Programs from 'bin'
#
CRUNCH_SRCDIRS+= bin
CRUNCH_PATH_bin= ${.CURDIR}/../..
CRUNCH_PROGS_bin=	\
	cat		\
	chmod		\
	cp		\
	cpdup		\
	date		\
	dd		\
	df		\
	echo		\
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
CRUNCH_ALIAS_test= [
CRUNCH_ALIAS_sh= -sh
# The -sh alias shouldn't appear as a link
CRUNCH_SUPPRESS_LINK_-sh= 1
CRUNCH_ALIAS_ln= link
CRUNCH_ALIAS_rm= unlink

# Programs from 'usr.bin'
#
CRUNCH_SRCDIRS+= usr.bin
CRUNCH_PATH_usr.bin= ${.CURDIR}/../..
CRUNCH_PROGS_usr.bin=	\
	awk		\
	basename	\
	bc		\
	bzip2		\
	cap_mkdb	\
	chflags		\
	cmp		\
	cut		\
	dirname		\
	du		\
	env		\
	false		\
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
	paste		\
	patch		\
	pkill		\
	printf		\
	relpath		\
	sed		\
	sort		\
	stat		\
	tail		\
	tee		\
	touch		\
	tput		\
	tr		\
	true		\
	truss		\
	uname		\
	undo		\
	uniq		\
	vi		\
	wc		\
	xz		\
	yes

CRUNCH_ALIAS_bzip2= bunzip2 bzcat
CRUNCH_ALIAS_gzip= gunzip gzcat zcat
CRUNCH_ALIAS_hexdump= hd od
CRUNCH_ALIAS_id= groups whoami
CRUNCH_ALIAS_less= more
CRUNCH_ALIAS_pkill= pgrep
CRUNCH_ALIAS_stat= readlink
CRUNCH_ALIAS_vi= ex nex nvi nview view
CRUNCH_ALIAS_xz= lzcat lzma unlzma unxz xzcat

# Programs from 'sbin'
#
CRUNCH_SRCDIRS+= sbin
CRUNCH_PATH_sbin= ${.CURDIR}/../..
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
	fsck_msdosfs	\
	gpt		\
	growfs		\
	ifconfig	\
	kldconfig	\
	kldload		\
	kldstat		\
	kldunload	\
	ldconfig	\
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
	rconfig		\
	rcorder		\
	reboot		\
	restore		\
	route		\
	savecore	\
	shutdown	\
	swapon		\
	sysctl		\
	tunefs		\
	udevd		\
	umount		\
	vinum

CRUNCH_ALIAS_disklabel64= disklabel
CRUNCH_ALIAS_dump= rdump
CRUNCH_ALIAS_newfs= mount_mfs
CRUNCH_ALIAS_reboot= halt
CRUNCH_ALIAS_restore= rrestore
CRUNCH_ALIAS_shutdown= poweroff
CRUNCH_ALIAS_swapon= swapoff swapctl

# Programs from 'usr.sbin'
#
CRUNCH_SRCDIRS+= usr.sbin
CRUNCH_PATH_usr.sbin= ${.CURDIR}/../..
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

CRUNCH_ALIAS_chown= chgrp
CRUNCH_ALIAS_rtsold= rtsol

CRUNCH_SRCDIRS+= gnu/sbin
CRUNCH_PATH_gnu/sbin= ${.CURDIR}/../..
CRUNCH_PROGS_gnu/sbin= dmsetup lvm

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

CRUNCH_SRCDIRS+= grep
CRUNCH_PATH_grep= ${.CURDIR}/../../gnu/usr.bin
CRUNCH_PROGS_grep= grep
CRUNCH_INTLIB_grep= ${CRUNCH_PATH_grep}/grep/libgreputils/libgreputils.a
SCRIPTS+= \
	${CRUNCH_PATH_grep}/grep/egrep/egrep \
	${CRUNCH_PATH_grep}/grep/fgrep/fgrep

CRUNCH_SRCDIRS+= diff
CRUNCH_PATH_diff= ${.CURDIR}/../../gnu/usr.bin
CRUNCH_PROGS_diff= diff diff3 sdiff
CRUNCH_INTLIB_diff= \
	${CRUNCH_PATH_diff}/diff/libdiffutils/libdiffutils.a \
	${CRUNCH_PATH_diff}/diff/libver/libver.a
CRUNCH_INTLIB_diff3= ${CRUNCH_INTLIB_diff}
CRUNCH_INTLIB_sdiff= ${CRUNCH_INTLIB_diff}

CRUNCH_LIBS+= -llvm -ldevmapper -ldevattr -ldm -lprop  # lvm
CRUNCH_LIBS+= -ldevstat  # vinum
CRUNCH_LIBS+= -lbsdxml  # ifconfig
CRUNCH_LIBS+= -lcam # camcontrol
CRUNCH_LIBS+= -lsbuf  # camcontrol, ifconfig
CRUNCH_LIBS+= -lkiconv  # mount_{cd9660,msdos,ntfs}
CRUNCH_LIBS+= -llzma  # xz
CRUNCH_LIBS+= -lbz2  # bzip2
CRUNCH_LIBS+= -lkvm  # ccdconfig, dmesg, pkill, ps
CRUNCH_LIBS+= -lcrypt  # pw
CRUNCH_LIBS+= -lprivate_edit  # bc, sh, vinum
#CRUNCH_LIBS+= -lprivate_ncurses  # bc, less, ls, tput, vinum (=> ncursesw)
CRUNCH_LIBS+= -lprivate_ncursesw  # vi
CRUNCH_LIBS+= -lutil
CRUNCH_LIBS+= -lz  # savecore
CRUNCH_LIBS+= -lm  # awk, ps, ping
CRUNCH_LIBS+= -lpthread
CRUNCH_LINKOPTS+= ${PRIVATELIB_LDFLAGS}

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
