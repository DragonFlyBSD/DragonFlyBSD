# ARM64 EFI Loader MVP Part 1 (Console Bring-up)

## Goal (MVP Part 1)

Produce an AArch64 UEFI loader binary that executes under QEMU+EDK2 and prints to the UEFI console. This is the first part of the MVP and does not load a kernel.

## Scope

- UEFI loader only (`BOOTAA64.EFI`)
- Console banner output via existing `efi_main`/`main`
- No kernel loading, no `boot1.efi`, no device autoload changes beyond what is required to build

## Implementation Plan

1. **Enable arm64 build gating**
   - Update `stand/boot/efi/Makefile` so `libefi` and `loader` build for `MACHINE_ARCH=aarch64`.

2. **Add arm64 arch scaffolding**
   - Create `stand/boot/efi/loader/arch/aarch64/Makefile.inc` with minimal sources and AArch64 UEFI include paths.
   - Add `stand/boot/efi/loader/arch/aarch64/start.S` to call `self_reloc(ImageBase, _DYNAMIC)` and `efi_main()`.
   - Add `stand/boot/efi/loader/arch/aarch64/ldscript.aarch64` mirroring the x86_64 linker script but using AArch64 ELF output.

3. **Adjust loader build glue**
   - In `stand/boot/efi/loader/Makefile`, set `EFI_TARGET=pei-aarch64` for arm64.
   - Build `i386_module.c` only on x86_64; keep `smbios.c` available for shared EFI code paths.

4. **Console output validation**
   - Rely on existing `stand/boot/efi/loader/main.c` output (banner, EFI firmware info).
   - Ensure `cons_probe()` runs successfully and `interact()` starts, confirming the loader is alive.

5. **Package as BOOTAA64.EFI**
   - Use the resulting `loader.efi` as `EFI/BOOT/BOOTAA64.EFI` on the ESP.

## Build Instructions (VM)

Assuming the tree is in `/usr/src` on the VM:

```sh
cd /usr/src
make -C stand/boot/efi/loader MACHINE_ARCH=aarch64 MACHINE=aarch64 MACHINE_PLATFORM=aarch64
```

To find the output directory:

```sh
make -C stand/boot/efi/loader -V .OBJDIR
```

The binary is `loader.efi` in that object directory.

## Copy to Host

```sh
scp -P 6021 root@devbox.sector.int:/path/to/obj/stand/boot/efi/loader/loader.efi ./BOOTAA64.EFI
```

## Stage on the ESP

Place it as:

`EFI/BOOT/BOOTAA64.EFI`

## Smoke Test

Boot QEMU with EDK2 and the ESP. Success for MVP Part 1 is the loader banner/EFI info printed on the UEFI console and an interactive prompt.
