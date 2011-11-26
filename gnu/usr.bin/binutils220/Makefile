SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as ld nm objcopy objdump ranlib readelf \
		size strings strip doc cxxfilt

TARGET_ARCH?=	${MACHINE_ARCH}

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
