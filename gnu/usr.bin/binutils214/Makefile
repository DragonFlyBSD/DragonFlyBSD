# $FreeBSD: src/gnu/usr.bin/binutils/Makefile,v 1.11.2.5 2002/09/01 23:39:12 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/Attic/Makefile,v 1.1 2004/02/01 08:53:00 dillon Exp $

# XXX gasp removed.  gasp does not exist as a separate entity
# in binutils-2.14
SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as ld nm objcopy objdump ranlib readelf \
		size strings strip doc

TARGET_ARCH?=	${MACHINE_ARCH}
.if (${TARGET_ARCH} == "alpha" || ${TARGET_ARCH} == "i386") \
    && !defined(NO_GDB)
#SUBDIR+=	gdb gdbreplay
.endif

.include <bsd.subdir.mk>
