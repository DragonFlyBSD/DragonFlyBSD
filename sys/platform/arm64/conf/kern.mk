# Placeholder for arm64 platform-specific kernel flags
INLINE_LIMIT?=	8000
CC=		/usr/local/bin/aarch64-none-elf-gcc
LD=		/usr/local/bin/aarch64-none-elf-ld
OBJCOPY=	/usr/local/bin/aarch64-none-elf-objcopy
AS=		/usr/local/bin/aarch64-none-elf-as
CFLAGS+=	-D__DragonFly__
CFLAGS+=	-D__x86_64__
