# $FreeBSD: src/sys/kern/Make.tags.inc,v 1.6.2.2 2003/02/10 12:21:08 des Exp $
# $DragonFly: src/sys/kern/Make.tags.inc,v 1.5 2003/08/07 21:54:30 dillon Exp $
#	@(#)Make.tags.inc	8.1 (Berkeley) 6/11/93

SYS?=	/sys

# Common files for "make tags", included by the Makefile for each
# architecture.

# Put the /sys/sys include files at the end so that subroutine definitions
# win when there is a struct tag with the same name (e.g., vmmeter).  The
# better solution would be for ctags to generate "struct vmmeter" tags.

COMM=	${SYS}/conf/*.[ch] \
	${SYS}/dev/advansys/*.[ch] \
	${SYS}/dev/aha/*.[ch] \
	${SYS}/dev/aic7xxx/*.[ch] \
	${SYS}/dev/buslogic/*.[ch] \
	${SYS}/dev/ccd/*.[ch] \
	${SYS}/dev/dec/*.[ch] \
	${SYS}/dev/dpt/*.[ch] \
	${SYS}/dev/en/*.[ch] \
	${SYS}/dev/hea/*.[ch] \
	${SYS}/dev/hfa/*.[ch] \
	${SYS}/dev/iicbus/*.[ch] \
	${SYS}/dev/isp/*.[ch] \
	${SYS}/dev/pdq/*.[ch] \
	${SYS}/dev/ppbus/*.[ch] \
	${SYS}/dev/smbus/*.[ch] \
	${SYS}/dev/vn/*.[ch] \
	${SYS}/dev/vx/*.[ch] \
	${SYS}/vfs/isofs/cd9660/*.[ch] \
	${SYS}/kern/*.[ch] \
	${SYS}/vfs/deadfs/*.[ch] \
	${SYS}/vfs/fdesc/*.[ch] \
	${SYS}/vfs/fifofs/*.[ch] \
	${SYS}/vfs/nullfs/*.[ch] \
	${SYS}/vfs/portal/*.[ch] \
	${SYS}/vfs/procfs/*.[ch] \
	${SYS}/vfs/specfs/*.[ch] \
	${SYS}/vfs/umapfs/*.[ch] \
	${SYS}/vfs/union/*.[ch] \
	${SYS}/vfs/msdosfs/*.[ch] \
	${SYS}/net/*.[ch] \
	${SYS}/netproto/atalk/*.[ch] \
	${SYS}/netproto/atm/*.[ch] \
	${SYS}/netinet/*.[ch] \
	${SYS}/netproto/ipx/*.[ch] \
	${SYS}/netproto/key/*.[ch] \
	${SYS}/netproto/natm/*.[ch] \
	${SYS}/netproto/ns/*.[ch] \
	${SYS}/vfs/nfs/*.[ch] \
	${SYS}/bus/pci/*.[ch] \
	${SYS}/emulation/posix4/*.[ch] \
	${SYS}/vfs/mfs/*.[ch] \
	${SYS}/vfs/ufs/*.[ch] \
	${SYS}/vm/*.[ch] \
	${SYS}/sys/*.[ch]

COMMDIR1= ${SYS}/conf \
	${SYS}/kern \
	${SYS}/net \
	${SYS}/netproto \
	${SYS}/vfs \
	${SYS}/pci \
	${SYS}/emulation \
	${SYS}/vm \
	${SYS}/sys

COMMDIR2= ${SYS}/dev/advansys \
	${SYS}/dev/aha \
	${SYS}/dev/aic7xxx \
	${SYS}/dev/buslogic \
	${SYS}/dev/ccd \
	${SYS}/dev/dec \
	${SYS}/dev/dpt \
	${SYS}/dev/en \
	${SYS}/dev/hea \
	${SYS}/dev/hfa \
	${SYS}/dev/iicbus \
	${SYS}/dev/isp \
	${SYS}/dev/pdq \
	${SYS}/dev/ppbus \
	${SYS}/dev/smbus \
	${SYS}/dev/vn \
	${SYS}/dev/vx \
	${SYS}/vfs/isofs/cd9660 \
	${SYS}/vfs/deadfs \
	${SYS}/vfs/fdesc \
	${SYS}/vfs/fifofs \
	${SYS}/vfs/nullfs \
	${SYS}/vfs/portal \
	${SYS}/vfs/procfs \
	${SYS}/vfs/specfs \
	${SYS}/vfs/umapfs \
	${SYS}/vfs/union \
	${SYS}/vfs/ufs \
	${SYS}/vfs/mfs
