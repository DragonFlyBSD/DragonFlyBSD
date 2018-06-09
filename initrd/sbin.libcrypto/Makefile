# $FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

# To include '../Makefine.inc' for use in <bsd.crunchgen.mk>
.include <bsd.init.mk>

PROG= rescue.libcrypto

CRUNCH_SRCDIRS+= sbin
CRUNCH_PATH_sbin= ${.CURDIR}/../..
CRUNCH_PROGS_sbin=	\
	md5		\
	hammer		\
	hammer2		\
	ping		\
	ping6		\
	tcplay

CRUNCH_ALIAS_md5= rmd160 sha1 sha256 sha512

CRUNCH_SRCDIRS+= usr.sbin/802_11
CRUNCH_PATH_usr.sbin/802_11= ${.CURDIR}/../..
CRUNCH_PROGS_usr.sbin/802_11= wpa_supplicant

CRUNCH_SRCDIRS+= gnu/sbin
CRUNCH_PATH_gnu/sbin= ${.CURDIR}/../..
CRUNCH_PROGS_gnu/sbin= cryptsetup lvm

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

CRUNCH_LIBS+= -llvm -ldevmapper -ldevattr  # lvm
CRUNCH_LIBS+= -ldm -lprop  # tcplay, cryptsetup, lvm
CRUNCH_LIBS+= -ldmsg -lutil -lhammer -lm
CRUNCH_LIBS+= -lmd  # md5, ping6
CRUNCH_LIBS+= -lpcap  # wpa_supplicant
CRUNCH_LIBS+= -lprivate_ssl  # wpa_supplicant
CRUNCH_LIBS+= -lprivate_crypto
CRUNCH_LIBS+= ${PRIVATELIB_LDFLAGS}

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
