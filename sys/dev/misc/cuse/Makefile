KMOD=	cuse

SRCS=	cuse.c device_if.h bus_if.h

# Simplfiy diffing this implementation against the version in FreeBSD.
# Simplify run "make dev-diff-freebsd" and review the changes.
dev-diff-freebsd: freebsd-cuse.c freebsd-cuse_defs.h freebsd-cuse_ioctl.h
	diff -u freebsd-cuse.c cuse.c || true
	diff -u freebsd-cuse_defs.h cuse_defs.h || true
	diff -u freebsd-cuse_ioctl.h cuse_ioctl.h || true

GH=https://raw.githubusercontent.com/freebsd/freebsd-src/refs/heads/main/sys/fs/cuse

.for FILE in cuse.c cuse_defs.h cuse_ioctl.h
freebsd-${FILE}:
	fetch -o ${.TARGET} ${GH}/${FILE}
.endfor

.include <bsd.kmod.mk>
