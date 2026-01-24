/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/linker.h>

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long uintptr_t;
typedef uintptr_t vm_offset_t;

#define	ARM64_KERNBASE	0xffffff8000000000ULL
#define	ARM64_PHYSBASE	0x0000000040000000ULL
#define	ARM64_PTOTV_OFF	(ARM64_KERNBASE - ARM64_PHYSBASE)

static volatile uint32_t *const uart_base = (uint32_t *)0x09000000;

static const uint32_t modinfo_end = 0x0000;
static const uint32_t modinfo_name = 0x0001;
static const uint32_t modinfo_metadata = 0x8000;
static const uint32_t modinfomd_kernend = 0x0008;

uintptr_t boot_modulep;
int boothowto;
char *kern_envp;
uintptr_t efi_systbl_phys;

extern caddr_t preload_metadata;
void preload_bootstrap_relocate(vm_offset_t offset);
caddr_t preload_search_by_type(const char *type);

static void
uart_putc(char ch)
{
	*uart_base = (uint32_t)(unsigned char)ch;
}

static void
uart_puts(const char *str)
{
	while (*str != '\0') {
		uart_putc(*str++);
	}
}

static void
uart_puthex(uint64_t value)
{
	const char *hex = "0123456789abcdef";
	int shift;

	for (shift = 60; shift >= 0; shift -= 4)
		uart_putc(hex[(value >> shift) & 0xf]);
}

static uintptr_t
roundup_uintptr(uintptr_t value, uintptr_t align)
{
	return (value + align - 1) & ~(align - 1);
}

static void
parse_modulep(uintptr_t modulep)
{
	uint32_t *hdr;
	uintptr_t next;
	const char *name;
	uintptr_t kernend;

	hdr = (uint32_t *)modulep;
	kernend = 0;

	for (;;) {
		if (hdr[0] == modinfo_end && hdr[1] == modinfo_end)
			break;
		if (hdr[0] == modinfo_name) {
			name = (const char *)(hdr + 2);
			uart_puts("[arm64] module: ");
			uart_puts(name);
			uart_puts("\r\n");
		} else if (hdr[0] == (modinfo_metadata | modinfomd_kernend)) {
			kernend = *(uintptr_t *)(hdr + 2);
		}
		next = sizeof(uint32_t) * 2 + hdr[1];
		next = roundup_uintptr(next, sizeof(uintptr_t));
		hdr = (uint32_t *)((uintptr_t)hdr + next);
	}

	if (kernend != 0) {
		uart_puts("[arm64] kernend=0x");
		uart_puthex((uint64_t)kernend);
		uart_puts("\r\n");
	}
}

void
initarm(uintptr_t modulep)
{
	boot_modulep = modulep;

	uart_puts("\033[2J\033[H");

	if (modulep == 0) {
		uart_puts("[arm64] initarm: modulep missing\r\n");
		return;
	}

	uart_puts("[arm64] initarm: modulep=0x");
	uart_puthex((uint64_t)modulep);
	uart_puts("\r\n");

	parse_modulep(modulep);

	preload_metadata = (caddr_t)(modulep + ARM64_PTOTV_OFF);
	preload_bootstrap_relocate(ARM64_PTOTV_OFF);

	caddr_t kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	if (kmdp == NULL) {
		uart_puts("[arm64] no kernel metadata\r\n");
		return;
	}

	boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
	kern_envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *) + ARM64_PTOTV_OFF;
	efi_systbl_phys = MD_FETCH(kmdp, MODINFOMD_FW_HANDLE, uintptr_t);

	uart_puts("[arm64] boothowto=0x");
	uart_puthex((uint64_t)boothowto);
	uart_puts("\r\n");
	uart_puts("[arm64] kern_envp=0x");
	uart_puthex((uint64_t)kern_envp);
	uart_puts("\r\n");
	uart_puts("[arm64] efi_systbl=0x");
	uart_puthex((uint64_t)efi_systbl_phys);
	uart_puts("\r\n");
}
