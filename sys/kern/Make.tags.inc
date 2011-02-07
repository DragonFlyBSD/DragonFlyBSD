# $FreeBSD: src/sys/kern/Make.tags.inc,v 1.6.2.2 2003/02/10 12:21:08 des Exp $
# $DragonFly: src/sys/kern/Make.tags.inc,v 1.8 2006/05/14 18:07:29 swildner Exp $
#	@(#)Make.tags.inc	8.1 (Berkeley) 6/11/93

SYS?=	/sys

# Common files for "make tags", included by the Makefile for each
# architecture.

# Put the /sys/sys include files at the end so that subroutine definitions
# win when there is a struct tag with the same name (e.g., vmmeter).  The
# better solution would be for ctags to generate "struct vmmeter" tags.

COMM=	${SYS}/conf/*.[ch] \
	${SYS}/dev/disk/advansys/*.[ch] \
	${SYS}/dev/disk/aha/*.[ch] \
	${SYS}/dev/disk/aic7xxx/*.[ch] \
	${SYS}/dev/disk/buslogic/*.[ch] \
	${SYS}/dev/disk/ccd/*.[ch] \
	${SYS}/dev/misc/dec/*.[ch] \
	${SYS}/dev/raid/dpt/*.[ch] \
	${SYS}/dev/atm/en/*.[ch] \
	${SYS}/dev/atm/hea/*.[ch] \
	${SYS}/dev/atm/hfa/*.[ch] \
	${SYS}/bus/iicbus/*.[ch] \
	${SYS}/dev/disk/isp/*.[ch] \
	${SYS}/bus/ppbus/*.[ch] \
	${SYS}/bus/smbus/*.[ch] \
	${SYS}/dev/disk/vn/*.[ch] \
	${SYS}/dev/netif/vx/*.[ch] \
	${SYS}/vfs/isofs/cd9660/*.[ch] \
	${SYS}/kern/*.[ch] \
	${SYS}/vfs/deadfs/*.[ch] \
	${SYS}/vfs/fdesc/*.[ch] \
	${SYS}/vfs/fifofs/*.[ch] \
	${SYS}/vfs/nullfs/*.[ch] \
	${SYS}/vfs/portal/*.[ch] \
	${SYS}/vfs/procfs/*.[ch] \
	${SYS}/vfs/specfs/*.[ch] \
	${SYS}/vfs/union/*.[ch] \
	${SYS}/vfs/msdosfs/*.[ch] \
	${SYS}/net/*.[ch] \
	${SYS}/netproto/atalk/*.[ch] \
	${SYS}/netproto/atm/*.[ch] \
	${SYS}/netinet/*.[ch] \
	${SYS}/netproto/ipx/*.[ch] \
	${SYS}/netproto/key/*.[ch] \
	${SYS}/netproto/natm/*.[ch] \
	${SYS}/vfs/nfs/*.[ch] \
	${SYS}/bus/pci/*.[ch] \
	${SYS}/emulation/posix4/*.[ch] \
	${SYS}/vfs/mfs/*.[ch] \
	${SYS}/vfs/ufs/*.[ch] \
	${SYS}/vfs/tmpfs/*.[ch] \
	${SYS}/vm/*.[ch] \
	${SYS}/sys/*.[ch]

COMMDIR1= ${SYS}/conf \
	${SYS}/kern \
	${SYS}/net \
	${SYS}/netproto \
	${SYS}/vfs \
	${SYS}/bus/pci \
	${SYS}/emulation \
	${SYS}/vm \
	${SYS}/sys

COMMDIR2= ${SYS}/dev/disk/advansys \
	${SYS}/dev/disk/aha \
	${SYS}/dev/disk/aic7xxx \
	${SYS}/dev/disk/buslogic \
	${SYS}/dev/disk/ccd \
	${SYS}/dev/misc/dec \
	${SYS}/dev/raid/dpt \
	${SYS}/dev/atm/en \
	${SYS}/dev/atm/hea \
	${SYS}/dev/atm/hfa \
	${SYS}/bus/iicbus \
	${SYS}/dev/disk/isp \
	${SYS}/bus/ppbus \
	${SYS}/bus/smbus \
	${SYS}/dev/disk/vn \
	${SYS}/dev/netif/vx \
	${SYS}/vfs/isofs/cd9660 \
	${SYS}/vfs/deadfs \
	${SYS}/vfs/fdesc \
	${SYS}/vfs/fifofs \
	${SYS}/vfs/nullfs \
	${SYS}/vfs/portal \
	${SYS}/vfs/procfs \
	${SYS}/vfs/specfs \
	${SYS}/vfs/union \
	${SYS}/vfs/ufs \
	${SYS}/vfs/mfs \
	${SYS}/vfs/tmpfs
