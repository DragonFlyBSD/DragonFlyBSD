# Makefile for testing DragonFly BSD arm64 EFI loader and kernel
#
# Usage:
#   make run          - Run loader interactively (Ctrl-A X to exit)
#   make run-gui      - Run loader with graphical display
#   make test         - Run loader with timeout (auto-exits after 25s)
#   make setup        - Create EFI disk structure and copy loader
#   make copy-loader  - Copy loader from VM to local test environment
#   make copy-kernel  - Copy kernel from VM to local test environment
#   make clean        - Remove temporary test files
#
# Requirements:
#   - qemu-system-aarch64
#   - EDK2 AARCH64 firmware (edk2-aarch64-code.fd)
#

# Paths - adjust these as needed
VM_DIR          ?= $(HOME)/vms
LOADER_EFI      ?= $(VM_DIR)/loader.efi
KERNEL_ELF      ?= $(VM_DIR)/kernel
DISK_IMAGE      ?= $(VM_DIR)/dfly.qcow2
EFI_TEST_DIR    ?= /tmp/efi_test
EFI_BOOT_DIR    ?= $(EFI_TEST_DIR)/EFI/BOOT
BOOTAA64_EFI    ?= $(EFI_BOOT_DIR)/BOOTAA64.EFI
STARTUP_NSH     ?= $(EFI_TEST_DIR)/startup.nsh

# QEMU settings
QEMU            ?= qemu-system-aarch64
QEMU_MACHINE    ?= virt
QEMU_CPU        ?= cortex-a72
QEMU_MEMORY     ?= 512M
QEMU_BIOS       ?= /opt/homebrew/share/qemu/edk2-aarch64-code.fd
TEST_TIMEOUT    ?= 25
EFI_SERIAL_LOG  ?= /tmp/efi-serial.log

# Remote VM (for copying fresh builds)
VM_HOST         ?= devbox.sector.int
VM_PORT         ?= 6021
VM_USER         ?= root
VM_LOADER_PATH  ?= /usr/src/stand/boot/efi/loader/loader.efi
VM_KERNEL_PATH  ?= /usr/src/sys/platform/arm64/aarch64/kernel

# QEMU base arguments (common to all modes)
QEMU_ARGS_BASE = \
	-M $(QEMU_MACHINE) \
	-cpu $(QEMU_CPU) \
	-m $(QEMU_MEMORY) \
	-bios $(QEMU_BIOS) \
	-serial file:$(EFI_SERIAL_LOG) \
	-serial stdio

# Console mode arguments
QEMU_ARGS = $(QEMU_ARGS_BASE) -serial stdio -device ramfb -display none
QEMU_ARGS_TEST = $(QEMU_ARGS)
QEMU_ARGS_GUI = $(QEMU_ARGS_BASE) -serial stdio -device ramfb -device qemu-xhci -device usb-kbd

# Drive configuration - use disk image if it exists, otherwise FAT directory
ifdef USE_DISK
QEMU_DRIVE = -drive file=$(DISK_IMAGE),format=qcow2,if=virtio
else
QEMU_DRIVE = -drive file=fat:rw:$(EFI_TEST_DIR),format=raw,media=disk
endif

.PHONY: all run run-gui test setup copy-loader copy-kernel copy-all clean help check-deps

all: help

help:
	@echo "DragonFly BSD arm64 EFI Test Environment"
	@echo ""
	@echo "Targets:"
	@echo "  make run          - Run loader interactively (Ctrl-A X to exit)"
	@echo "  make run-gui      - Run loader with graphical display"
	@echo "  make test         - Run loader with $(TEST_TIMEOUT)s timeout"
	@echo "  make setup        - Create EFI disk structure and copy loader"
	@echo "  make copy-loader  - Copy loader from remote VM"
	@echo "  make copy-kernel  - Copy kernel from remote VM"
	@echo "  make copy-all     - Copy both loader and kernel from remote VM"
	@echo "  make clean        - Remove temporary test files"
	@echo ""
	@echo "Variables (override with VAR=value):"
	@echo "  VM_DIR=$(VM_DIR)"
	@echo "  LOADER_EFI=$(LOADER_EFI)"
	@echo "  KERNEL_ELF=$(KERNEL_ELF)"
	@echo "  DISK_IMAGE=$(DISK_IMAGE)"
	@echo "  TEST_TIMEOUT=$(TEST_TIMEOUT)"
	@echo "  USE_DISK=1        - Use qcow2 disk instead of FAT directory"
	@echo ""
	@echo "Examples:"
	@echo "  make copy-all && make test"
	@echo "  make run-gui"
	@echo "  make run USE_DISK=1"

check-deps:
	@command -v $(QEMU) >/dev/null 2>&1 || \
		{ echo "Error: $(QEMU) not found"; exit 1; }
	@test -f $(QEMU_BIOS) || \
		{ echo "Error: EDK2 firmware not found at $(QEMU_BIOS)"; exit 1; }

# Setup the EFI test directory structure
setup: check-deps
	@echo "Setting up EFI test environment..."
	@mkdir -p $(EFI_BOOT_DIR)
	@if [ -f "$(LOADER_EFI)" ]; then \
		cp $(LOADER_EFI) $(BOOTAA64_EFI); \
		echo "Copied loader to $(BOOTAA64_EFI)"; \
	else \
		echo "Warning: $(LOADER_EFI) not found"; \
		echo "Run 'make copy-loader' to fetch from VM"; \
	fi
	@echo "Setup complete."

# Copy loader from remote VM
copy-loader:
	@echo "Copying loader from $(VM_USER)@$(VM_HOST)..."
	@mkdir -p $(VM_DIR)
	@mkdir -p $(EFI_BOOT_DIR)
	scp -P $(VM_PORT) $(VM_USER)@$(VM_HOST):$(VM_LOADER_PATH) $(LOADER_EFI)
	cp $(LOADER_EFI) $(BOOTAA64_EFI)
	@echo "Loader copied to $(LOADER_EFI) and $(BOOTAA64_EFI)"

# Copy kernel from remote VM
copy-kernel:
	@echo "Copying kernel from $(VM_USER)@$(VM_HOST)..."
	@mkdir -p $(VM_DIR)
	@mkdir -p $(EFI_TEST_DIR)
	scp -P $(VM_PORT) $(VM_USER)@$(VM_HOST):$(VM_KERNEL_PATH) $(KERNEL_ELF)
	cp $(KERNEL_ELF) $(EFI_TEST_DIR)/kernel
	@echo "Kernel copied to $(KERNEL_ELF) and $(EFI_TEST_DIR)/kernel"

# Copy both loader and kernel
copy-all: copy-loader copy-kernel
	@echo "Both loader and kernel copied successfully"

# Run QEMU interactively
run: check-deps
	@test -f $(BOOTAA64_EFI) || \
		{ echo "Error: $(BOOTAA64_EFI) not found. Run 'make setup' first."; exit 1; }
	@printf "BOOTAA64.EFI console=eficom\r\n" > $(STARTUP_NSH)
	@echo "Starting QEMU (press Ctrl-A X to exit)..."
	$(QEMU) $(QEMU_ARGS) $(QEMU_DRIVE)

# Run QEMU with graphical display
run-gui: check-deps
	@test -f $(BOOTAA64_EFI) || \
		{ echo "Error: $(BOOTAA64_EFI) not found. Run 'make setup' first."; exit 1; }
	@printf "BOOTAA64.EFI\r\n" > $(STARTUP_NSH)
	@echo "Starting QEMU with graphical display (close window to exit)..."
	$(QEMU) $(QEMU_ARGS_GUI) $(QEMU_DRIVE)

# Run QEMU with timeout for automated testing
test: check-deps
	@test -f $(BOOTAA64_EFI) || \
		{ echo "Error: $(BOOTAA64_EFI) not found. Run 'make setup' first."; exit 1; }
	@printf "BOOTAA64.EFI console=eficom\r\n" > $(STARTUP_NSH)
	@echo "Starting QEMU with $(TEST_TIMEOUT)s timeout..."
	@( $(QEMU) $(QEMU_ARGS_TEST) $(QEMU_DRIVE) & \
		QEMU_PID=$$!; \
		sleep $(TEST_TIMEOUT); \
		kill $$QEMU_PID 2>/dev/null ) 2>&1

# Clean up temporary files
clean:
	@echo "Cleaning up..."
	rm -rf $(EFI_TEST_DIR)
	@echo "Note: $(VM_DIR) preserved. Remove manually if needed."
