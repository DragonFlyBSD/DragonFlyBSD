# $DragonFly: src/gnu/usr.bin/binutils215/Attic/Makefile,v 1.1 2004/12/20 13:14:44 asmodai Exp $

SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as ld nm objcopy objdump ranlib readelf \
		size strings strip doc

TARGET_ARCH?=	${MACHINE_ARCH}
.if (${TARGET_ARCH} == "alpha" || ${TARGET_ARCH} == "i386") \
    && !defined(NO_GDB)
#SUBDIR+=	gdb gdbreplay
.endif

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
