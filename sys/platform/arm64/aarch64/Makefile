# Makefile for ARM64 stub kernel
#
# This builds a minimal ELF64 kernel that prints to UART and halts.
# Used for testing the EFI loader's kernel loading capability.
#
# Build: make
# Clean: make clean
#

# Cross-compiler (must match what's on the VM)
CC=		aarch64-none-elf-gcc
AS=		aarch64-none-elf-as
LD=		aarch64-none-elf-ld
OBJCOPY=	aarch64-none-elf-objcopy
OBJDUMP=	aarch64-none-elf-objdump

# Output
KERNEL=		kernel

# Source files
SRCS=		locore.S machdep.c
OBJS=		locore.o machdep.o

# Kernel load address - must match what the loader expects
# Using 0x40000000 (1GB) as a reasonable physical address for QEMU virt
KERNEL_LOADADDR=	0x40100000

# Compiler flags
CFLAGS=		-ffreestanding -nostdlib -O2 -fno-builtin

# Assembler flags
ASFLAGS=	-march=armv8-a

# Linker flags - create a static ELF64 executable
LDFLAGS=	-nostdlib -static -T ldscript.aarch64 -e _start

.PHONY: all clean dump

all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Kernel built: $@"
	@$(OBJDUMP) -f $@ | grep -E '(file format|start address)'

locore.o: locore.S
	$(AS) $(ASFLAGS) -o $@ $<

machdep.o: machdep.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Show disassembly for debugging
dump: $(KERNEL)
	$(OBJDUMP) -d $(KERNEL)

# Show ELF headers
headers: $(KERNEL)
	$(OBJDUMP) -x $(KERNEL)

clean:
	rm -f $(KERNEL) $(OBJS)
