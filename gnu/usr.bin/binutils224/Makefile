SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as ld nm objcopy objdump ranlib readelf \
		elfedit size strings strip cxxfilt gprof \
		libgold gold incdump

TARGET_ARCH?=	${MACHINE_ARCH}

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
