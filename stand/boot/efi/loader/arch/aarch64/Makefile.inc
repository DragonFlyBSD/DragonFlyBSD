SRCS+=	start.S \
	elf64_freebsd.c

# Disable stack protector
CFLAGS+=	-fno-stack-protector

CFLAGS+=	-fPIC
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/MdePkg/Include
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/MdePkg/Include/AArch64
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/MdeModulePkg/Include

# Use the cross-toolchain objcopy for PE conversion
OBJCOPY=	aarch64-none-elf-objcopy
