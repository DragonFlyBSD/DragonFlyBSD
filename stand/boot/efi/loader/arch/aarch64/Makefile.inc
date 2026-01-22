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

# Force 4KB page size to match PE section alignment
# Without this, ld uses 64KB MAXPAGESIZE creating a gap between file offset and VMA
LDFLAGS+=	-Wl,-z,max-page-size=4096
