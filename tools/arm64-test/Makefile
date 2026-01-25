# Makefile for testing DragonFly BSD arm64 EFI loader and kernel
#
# Usage:
#   make run          - Run loader interactively (Ctrl-A X to exit)
#   make run-gui      - Run loader with graphical display
#   make test         - Run loader with timeout (auto-exits after TEST_TIMEOUT)
#   make copy-loader  - Copy loader from VM
#   make copy-kernel  - Copy kernel from VM (regenerates stripped version)
#   make copy-all     - Copy both loader and kernel from VM
#   make clean        - Remove EFI test directory
#
# Requirements:
#   - qemu-system-aarch64
#   - EDK2 AARCH64 firmware (edk2-aarch64-code.fd)
#

# All files stored under VM_DIR
VM_DIR          ?= $(HOME)/vms
EFI_DIR         := $(VM_DIR)/efi_test
EFI_BOOT_DIR    := $(EFI_DIR)/EFI/BOOT
KERNEL_DIR      := $(EFI_DIR)/boot/kernel

LOADER_EFI      := $(VM_DIR)/loader.efi
KERNEL_ELF      := $(VM_DIR)/kernel
BOOTAA64_EFI    := $(EFI_BOOT_DIR)/BOOTAA64.EFI
STARTUP_NSH     := $(EFI_DIR)/startup.nsh

# QEMU settings
QEMU            ?= qemu-system-aarch64
QEMU_MACHINE    ?= virt
QEMU_CPU        ?= cortex-a72
QEMU_MEMORY     ?= 512M
QEMU_BIOS       ?= /opt/homebrew/share/qemu/edk2-aarch64-code.fd
TEST_TIMEOUT    ?= 45

# Remote VM (for copying fresh builds)
VM_HOST         ?= devbox.sector.int
VM_PORT         ?= 6021
VM_USER         ?= root
VM_LOADER_PATH  ?= /usr/src/stand/boot/efi/loader/loader.efi
VM_KERNEL_PATH  ?= /usr/src/sys/compile/ARM64_GENERIC/kernel.stripped

# QEMU arguments
QEMU_ARGS_BASE = \
	-M $(QEMU_MACHINE) \
	-cpu $(QEMU_CPU) \
	-m $(QEMU_MEMORY) \
	-bios $(QEMU_BIOS) \
	-monitor none \
	-serial stdio

QEMU_ARGS     = $(QEMU_ARGS_BASE) -device ramfb -display none
QEMU_ARGS_GUI = $(QEMU_ARGS_BASE) -device ramfb -device qemu-xhci -device usb-kbd
QEMU_DRIVE    = -drive file=fat:rw:$(EFI_DIR),format=raw,media=disk

.PHONY: all run run-gui test copy-loader copy-kernel copy-all clean help check-deps

all: help

help:
	@echo "DragonFly BSD arm64 EFI Test Environment"
	@echo ""
	@echo "All files stored under: $(VM_DIR)"
	@echo "  $(EFI_DIR)/           - EFI test filesystem"
	@echo "  $(LOADER_EFI)         - Cached loader"
	@echo "  $(KERNEL_ELF)         - Cached kernel"
	@echo ""
	@echo "Targets:"
	@echo "  make run          - Run interactively (Ctrl-A X to exit)"
	@echo "  make run-gui      - Run with graphical display"
	@echo "  make test         - Run with $(TEST_TIMEOUT)s timeout"
	@echo "  make copy-loader  - Copy loader from remote VM"
	@echo "  make copy-kernel  - Copy kernel from remote VM"
	@echo "  make copy-all     - Copy both loader and kernel"
	@echo "  make clean        - Remove EFI test directory"
	@echo ""
	@echo "Variables:"
	@echo "  VM_DIR=$(VM_DIR)"
	@echo "  TEST_TIMEOUT=$(TEST_TIMEOUT)"
	@echo ""
	@echo "Example workflow:"
	@echo "  make copy-all && make test"

check-deps:
	@command -v $(QEMU) >/dev/null 2>&1 || \
		{ echo "Error: $(QEMU) not found"; exit 1; }
	@test -f $(QEMU_BIOS) || \
		{ echo "Error: EDK2 firmware not found at $(QEMU_BIOS)"; exit 1; }

# Copy loader from remote VM
copy-loader:
	@echo "Copying loader from $(VM_USER)@$(VM_HOST)..."
	@mkdir -p $(VM_DIR) $(EFI_BOOT_DIR)
	scp -P $(VM_PORT) $(VM_USER)@$(VM_HOST):$(VM_LOADER_PATH) $(LOADER_EFI)
	cp $(LOADER_EFI) $(BOOTAA64_EFI)
	@echo "Done: $(BOOTAA64_EFI)"

# Copy kernel from remote VM (regenerates stripped version first)
copy-kernel:
	@echo "Copying kernel from $(VM_USER)@$(VM_HOST)..."
	@mkdir -p $(VM_DIR) $(KERNEL_DIR)
	@echo "Regenerating stripped kernel on VM..."
	ssh -p $(VM_PORT) $(VM_USER)@$(VM_HOST) \
		"cd /usr/src/sys/compile/ARM64_GENERIC && \
		 /usr/local/bin/aarch64-none-elf-strip -o kernel.stripped kernel.debug"
	scp -P $(VM_PORT) $(VM_USER)@$(VM_HOST):$(VM_KERNEL_PATH) $(KERNEL_ELF)
	cp $(KERNEL_ELF) $(KERNEL_DIR)/kernel
	@echo "Done: $(KERNEL_DIR)/kernel"

# Copy both loader and kernel
copy-all: copy-loader copy-kernel
	@echo "Both loader and kernel copied to $(EFI_DIR)"

# Run QEMU interactively
run: check-deps
	@test -f $(BOOTAA64_EFI) || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@test -f $(KERNEL_DIR)/kernel || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@printf "BOOTAA64.EFI\r\n" > $(STARTUP_NSH)
	@echo "Starting QEMU (Ctrl-A X to exit)..."
	$(QEMU) $(QEMU_ARGS) $(QEMU_DRIVE)

# Run QEMU with graphical display
run-gui: check-deps
	@test -f $(BOOTAA64_EFI) || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@test -f $(KERNEL_DIR)/kernel || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@printf "BOOTAA64.EFI\r\n" > $(STARTUP_NSH)
	@echo "Starting QEMU with GUI..."
	$(QEMU) $(QEMU_ARGS_GUI) $(QEMU_DRIVE)

# Run QEMU with timeout for automated testing
test: check-deps
	@test -f $(BOOTAA64_EFI) || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@test -f $(KERNEL_DIR)/kernel || { echo "Error: Run 'make copy-all' first"; exit 1; }
	@printf "BOOTAA64.EFI\r\n" > $(STARTUP_NSH)
	@echo "Running QEMU with $(TEST_TIMEOUT)s timeout..."
	@( $(QEMU) $(QEMU_ARGS) $(QEMU_DRIVE) & \
		QEMU_PID=$$!; \
		sleep $(TEST_TIMEOUT); \
		kill $$QEMU_PID 2>/dev/null ) 2>&1

# Clean up EFI test directory
clean:
	@echo "Removing $(EFI_DIR)..."
	rm -rf $(EFI_DIR)
	@echo "Cached files preserved: $(LOADER_EFI) $(KERNEL_ELF)"
