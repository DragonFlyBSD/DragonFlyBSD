# $Id: Makefile,v 1.1 2004/08/13 18:30:24 max Exp $

KMOD=	vkbd
SRCS=	vkbd.c opt_kbd.h

.if !defined(KERNBUILDDIR)
opt_kbd.h:
	echo "#define KBD_INSTALL_CDEV 1" > ${.TARGET}
.endif

# Simplfiy diffing this implementation against the version in FreeBSD.
# Simplify run "make dev-diff-freebsd" and review the changes.
dev-diff-freebsd: freebsd-vkbd.c freebsd-vkbd_var.h
	diff -u freebsd-vkbd.c vkbd.c || true
	diff -u freebsd-vkbd_var.h vkbd_var.h || true

GH=https://raw.githubusercontent.com/freebsd/freebsd-src/refs/heads/main/sys/dev/vkbd

.for FILE in vkbd.c vkbd_var.h
freebsd-${FILE}:
	fetch -o ${.TARGET} ${GH}/${FILE}
.endfor



.include <bsd.kmod.mk>
