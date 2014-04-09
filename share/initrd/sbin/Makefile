#$FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

NO_MAN=	YES

.include <bsd.own.mk>

PROG=	rescue.sbin
BINDIR=	/usr/share/initrd/sbin

SCRIPTS= ../../../sbin/dhclient/dhclient-script

CRUNCH_SRCDIRS+= sbin
CRUNCH_PROGS_sbin=	\
	camcontrol	\
	ccdconfig	\
	dhclient	\
	diskinfo	\
	disklabel32	\
	disklabel64	\
	dmesg		\
	dumpfs		\
	dumpon		\
	fdisk		\
	ffsinfo		\
	fsck		\
	fsck_msdosfs	\
	gpt		\
	growfs		\
	hammer		\
	ifconfig	\
	kldconfig	\
	kldload		\
	kldstat		\
	kldunload	\
	md5		\
	mount		\
	mount_cd9660	\
	mount_hammer	\
	mount_msdos	\
	mount_nfs	\
	mount_ntfs	\
	mount_null	\
	mount_tmpfs	\
	mount_ufs	\
	natacontrol	\
	newfs		\
	newfs_hammer	\
	newfs_msdos	\
	ping		\
	ping6		\
	rconfig		\
	reboot		\
	route		\
	shutdown	\
	swapon		\
	sysctl		\
	tunefs		\
	udevd		\
	umount		\
	vinum

CRUNCH_ALIAS_disklabel64= disklabel
CRUNCH_ALIAS_md5= rmd160
CRUNCH_ALIAS_newfs= mount_mfs
CRUNCH_ALIAS_reboot= halt
CRUNCH_ALIAS_shutdown= poweroff
CRUNCH_ALIAS_swapon= swapoff swapctl

CRUNCH_LIBS+= -lcam -lsbuf -lutil -lkvm -lm -lmd -lhammer -lkiconv -ledit
CRUNCH_LIBS+= -ltermcap -ldevstat -lprop -lpthread
CRUNCH_LIBS+= -lipx -lbsdxml -lipsec

CRUNCH_SRCDIRS+= usr.sbin
CRUNCH_PROGS_usr.sbin= boot0cfg chroot

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
