# Placeholder for arm64 platform-specific kernel flags
INLINE_LIMIT?=	8000
CC=		/usr/local/bin/aarch64-none-elf-gcc
LD=		/usr/local/bin/aarch64-none-elf-ld
OBJCOPY=	/usr/local/bin/aarch64-none-elf-objcopy
AS=		/usr/local/bin/aarch64-none-elf-as
CFLAGS+=	-D__DragonFly__
# Use inline atomics instead of outline atomics to avoid needing
# __aarch64_cas*_acq_rel and __aarch64_ldadd*_acq_rel library functions
CFLAGS+=	-mno-outline-atomics
