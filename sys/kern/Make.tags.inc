# $FreeBSD: src/sys/kern/Make.tags.inc,v 1.6.2.2 2003/02/10 12:21:08 des Exp $
# $DragonFly: src/sys/kern/Make.tags.inc,v 1.4 2003/08/07 21:17:23 dillon Exp $
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
	${SYS}/isofs/cd9660/*.[ch] \
	${SYS}/kern/*.[ch] \
	${SYS}/miscfs/deadfs/*.[ch] \
	${SYS}/miscfs/fdesc/*.[ch] \
	${SYS}/miscfs/fifofs/*.[ch] \
	${SYS}/miscfs/nullfs/*.[ch] \
	${SYS}/miscfs/portal/*.[ch] \
	${SYS}/miscfs/procfs/*.[ch] \
	${SYS}/miscfs/specfs/*.[ch] \
	${SYS}/miscfs/umapfs/*.[ch] \
	${SYS}/miscfs/union/*.[ch] \
	${SYS}/msdosfs/*.[ch] \
	${SYS}/net/*.[ch] \
	${SYS}/netatalk/*.[ch] \
	${SYS}/netatm/*.[ch] \
	${SYS}/netinet/*.[ch] \
	${SYS}/netipx/*.[ch] \
	${SYS}/netkey/*.[ch] \
	${SYS}/netnatm/*.[ch] \
	${SYS}/netns/*.[ch] \
	${SYS}/nfs/*.[ch] \
	${SYS}/pci/*.[ch] \
	${SYS}/posix4/*.[ch] \
	${SYS}/vfs/mfs/*.[ch] \
	${SYS}/vfs/ufs/*.[ch] \
	${SYS}/vm/*.[ch] \
	${SYS}/sys/*.[ch]

COMMDIR1= ${SYS}/conf \
	${SYS}/kern \
	${SYS}/msdosfs \
	${SYS}/net \
	${SYS}/netatalk \
	${SYS}/netatm \
	${SYS}/netinet \
	${SYS}/netipx \
	${SYS}/netkey \
	${SYS}/netnatm \
	${SYS}/netns \
	${SYS}/nfs \
	${SYS}/pci \
	${SYS}/posix4 \
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
	${SYS}/isofs/cd9660 \
	${SYS}/miscfs/deadfs \
	${SYS}/miscfs/fdesc \
	${SYS}/miscfs/fifofs \
	${SYS}/miscfs/nullfs \
	${SYS}/miscfs/portal \
	${SYS}/miscfs/procfs \
	${SYS}/miscfs/specfs \
	${SYS}/miscfs/umapfs \
	${SYS}/miscfs/union \
	${SYS}/vfs/ufs \
	${SYS}/vfs/mfs
