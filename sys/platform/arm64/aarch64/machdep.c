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
static int
md_strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return ((unsigned char)*a - (unsigned char)*b);
}

#define	ARM64_KERNBASE	0xffffff8000000000ULL
#define	ARM64_PHYSBASE	0x0000000040000000ULL
#define	ARM64_PTOTV_OFF	(ARM64_KERNBASE - ARM64_PHYSBASE)

typedef u_long vm_offset_t;

#define	PTE_BLOCK_NORMAL_FLAGS	0x705

#define	MODINFOMD_EFI_MAP	0x1004

struct efi_map_header {
	u_int64_t	memory_size;
	u_int64_t	descriptor_size;
	u_int32_t	descriptor_version;
};

struct efi_md {
	u_int32_t	type;
	u_int32_t	pad;
	u_int64_t	phys_start;
	u_int64_t	virt_start;
	u_int64_t	num_pages;
	u_int64_t	attribute;
};

#define	EFI_MD_TYPE_CONVENTIONAL	7

#define	ARM64_MAX_PHYSMEM_RANGES	16

struct arm64_phys_range {
	u_int64_t	start;
	u_int64_t	end;
};

static struct arm64_phys_range arm64_physmem[ARM64_MAX_PHYSMEM_RANGES];
static int arm64_physmem_count;

static u_int64_t arm64_boot_alloc_base;
static u_int64_t arm64_boot_alloc_end;
static u_int64_t arm64_boot_alloc_next;

static void
arm64_zero_page(u_int64_t addr)
{
	volatile u_int64_t *p = (volatile u_int64_t *)(uintptr_t)addr;
	for (u_int64_t i = 0; i < (4096 / sizeof(u_int64_t)); i++)
		p[i] = 0;
}

static u_int64_t
arm64_boot_alloc(u_int64_t size, u_int64_t align)
{
	u_int64_t addr;

	if (align == 0)
		align = 4096;
	addr = roundup(arm64_boot_alloc_next, align);
	if (addr + size > arm64_boot_alloc_end)
		return (0);
	arm64_boot_alloc_next = addr + size;
	return (addr);
}

static void uart_puts(const char *str);
static void uart_puthex(u_int64_t value);

static void
arm64_pmap_bootstrap(struct arm64_phys_range *ranges, int count)
{
	u_int64_t best_size;
	int best;

	if (count == 0) {
		uart_puts("[arm64] pmap bootstrap: no ranges\r\n");
		return;
	}

	best = 0;
	best_size = ranges[0].end - ranges[0].start;
	for (int i = 1; i < count; i++) {
		u_int64_t size = ranges[i].end - ranges[i].start;
		if (size > best_size) {
			best = i;
			best_size = size;
		}
	}

	uart_puts("[arm64] pmap bootstrap: largest range 0x");
	uart_puthex(ranges[best].start);
	uart_puts("-0x");
	uart_puthex(ranges[best].end);
	uart_puts("\r\n");

	arm64_boot_alloc_base = roundup(ranges[best].start, 4096);
	arm64_boot_alloc_end = ranges[best].end;
	arm64_boot_alloc_next = arm64_boot_alloc_base;

	uart_puts("[arm64] boot_alloc range 0x");
	uart_puthex(arm64_boot_alloc_base);
	uart_puts("-0x");
	uart_puthex(arm64_boot_alloc_end);
	uart_puts("\r\n");

	u_int64_t scratch = arm64_boot_alloc(2 * 4096, 4096);
	if (scratch != 0) {
		uart_puts("[arm64] boot_alloc scratch 0x");
		uart_puthex(scratch);
		uart_puts("-0x");
		uart_puthex(scratch + (2 * 4096));
		uart_puts("\r\n");
	} else {
		uart_puts("[arm64] boot_alloc scratch failed\r\n");
	}

	u_int64_t pt = arm64_boot_alloc(2 * 4096, 4096);
	if (pt != 0) {
		uart_puts("[arm64] boot_alloc pt 0x");
		uart_puthex(pt);
		uart_puts("-0x");
		uart_puthex(pt + (2 * 4096));
		uart_puts("\r\n");

		arm64_zero_page(pt);
		arm64_zero_page(pt + 4096);
		((u_int64_t *)(uintptr_t)pt)[0] =
		    ((pt + 4096) & ~0xfffULL) | 0x3;
		((u_int64_t *)(uintptr_t)(pt + 4096))[0] =
		    ARM64_PHYSBASE | PTE_BLOCK_NORMAL_FLAGS;
		uart_puts("[arm64] pt l0[0]=0x");
		uart_puthex(((u_int64_t *)(uintptr_t)pt)[0]);
		uart_puts("\r\n");
		uart_puts("[arm64] pt l1[0]=0x");
		uart_puthex(((u_int64_t *)(uintptr_t)(pt + 4096))[0]);
		uart_puts("\r\n");
	} else {
		uart_puts("[arm64] boot_alloc pt failed\r\n");
	}
}

static volatile u_int32_t *const uart_base = (u_int32_t *)0x09000000;

static const u_int32_t modinfo_end = 0x0000;
static const u_int32_t modinfo_name = 0x0001;
static const u_int32_t modinfo_metadata = 0x8000;
static const u_int32_t modinfomd_kernend = 0x0008;

uintptr_t boot_modulep;
int boothowto;
char *kern_envp;
uintptr_t efi_systbl_phys;

static caddr_t preload_metadata;
static caddr_t preload_kmdp;

static void
preload_bootstrap_relocate(vm_offset_t offset)
{
	caddr_t curp;
	u_int32_t *hdr;
	vm_offset_t *ptr;
	int next;

	if (preload_metadata == NULL)
		return;

	curp = preload_metadata;
	for (;;) {
		hdr = (u_int32_t *)curp;
		if (hdr[0] == 0 && hdr[1] == 0)
			break;
		switch (hdr[0]) {
		case MODINFO_ADDR:
		case MODINFO_METADATA | MODINFOMD_SSYM:
		case MODINFO_METADATA | MODINFOMD_ESYM:
			ptr = (vm_offset_t *)(curp + (sizeof(u_int32_t) * 2));
			*ptr += offset;
			break;
		}
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup(next, sizeof(u_long));
		curp += next;
	}
}

static caddr_t
preload_search_by_type(const char *type)
{
	caddr_t curp, lname;
	u_int32_t *hdr;
	int next;

	if (preload_metadata == NULL)
		return (NULL);

	curp = preload_metadata;
	lname = NULL;
	for (;;) {
		hdr = (u_int32_t *)curp;
		if (hdr[0] == 0 && hdr[1] == 0)
			break;
		if (hdr[0] == MODINFO_NAME)
			lname = curp;
		if (hdr[0] == MODINFO_TYPE &&
		    md_strcmp(type, curp + sizeof(u_int32_t) * 2) == 0)
			return (lname);
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup(next, sizeof(u_long));
		curp += next;
	}
	return (NULL);
}

static void
preload_initkmdp(void)
{
	caddr_t curp, lname;
	u_int32_t *hdr;
	int next;

	preload_kmdp = NULL;
	if (preload_metadata == NULL)
		return;

	curp = preload_metadata;
	lname = NULL;
	for (;;) {
		hdr = (u_int32_t *)curp;
		if (hdr[0] == 0 && hdr[1] == 0)
			break;
		if (hdr[0] == MODINFO_NAME)
			lname = curp;
		if (hdr[0] == MODINFO_TYPE &&
		    (md_strcmp("elf kernel", curp + sizeof(u_int32_t) * 2) == 0 ||
		    md_strcmp("elf64 kernel", curp + sizeof(u_int32_t) * 2) == 0)) {
			preload_kmdp = lname;
			return;
		}
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup(next, sizeof(u_long));
		curp += next;
	}
}

static caddr_t
preload_search_info(caddr_t mod, int inf)
{
	caddr_t curp;
	u_int32_t *hdr;
	u_int32_t type = 0;
	int next;

	curp = mod;
	for (;;) {
		hdr = (u_int32_t *)curp;
		if (hdr[0] == 0 && hdr[1] == 0)
			break;
		if (type == 0) {
			type = hdr[0];
		} else if (hdr[0] == type) {
			break;
		}
		if (hdr[0] == inf)
			return (curp + (sizeof(u_int32_t) * 2));
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup(next, sizeof(u_long));
		curp += next;
	}
	return (NULL);
}

static uintptr_t
md_fetch_uintptr(caddr_t mdp, int info)
{
	uintptr_t *ptr;

	ptr = (uintptr_t *)preload_search_info(mdp, MODINFO_METADATA | info);
	return (ptr == NULL) ? 0 : *ptr;
}

static void *
md_fetch_ptr(caddr_t mdp, int info)
{
	return preload_search_info(mdp, MODINFO_METADATA | info);
}

static int
md_fetch_int(caddr_t mdp, int info)
{
	int *ptr;

	ptr = (int *)preload_search_info(mdp, MODINFO_METADATA | info);
	return (ptr == NULL) ? 0 : *ptr;
}

static void
uart_putc(char ch)
{
	*uart_base = (u_int32_t)(unsigned char)ch;
}

static void
uart_puts(const char *str)
{
	while (*str != '\0') {
		uart_putc(*str++);
	}
}

static void
uart_puthex(u_int64_t value)
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
dump_modulep_headers(const char *label, uintptr_t modulep)
{
	u_int32_t *hdr;
	uintptr_t next;
	int i;

	hdr = (u_int32_t *)modulep;

	uart_puts("[arm64] ");
	uart_puts(label);
	uart_puts(" header: ");
	for (i = 0; i < 4; i++) {
		uart_puthex(hdr[0]);
		uart_putc(' ');
		uart_puthex(hdr[1]);
		uart_putc(' ');
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup_uintptr(next, sizeof(uintptr_t));
		hdr = (u_int32_t *)((uintptr_t)hdr + next);
	}
	uart_puts("\r\n");

}

static void
parse_modulep(uintptr_t modulep)
{
	u_int32_t *hdr;
	uintptr_t next;
	const char *name;
	uintptr_t kernend;

	dump_modulep_headers("modulep", modulep);
	if (modulep > ARM64_PTOTV_OFF)
		dump_modulep_headers("modulep-pa", modulep - ARM64_PTOTV_OFF);

	hdr = (u_int32_t *)modulep;
	kernend = 0;

	hdr = (u_int32_t *)modulep;

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
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup_uintptr(next, sizeof(uintptr_t));
		hdr = (u_int32_t *)((uintptr_t)hdr + next);
	}

	if (kernend != 0) {
		uart_puts("[arm64] kernend=0x");
		uart_puthex((u_int64_t)kernend);
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

	if (modulep < ARM64_KERNBASE)
		modulep += ARM64_PTOTV_OFF;

	uart_puts("[arm64] initarm: modulep=0x");
	uart_puthex((u_int64_t)modulep);
	uart_puts("\r\n");

	parse_modulep(modulep);

	preload_metadata = (caddr_t)modulep;
	preload_bootstrap_relocate(0);
	preload_initkmdp();
	if (preload_kmdp == NULL) {
		uart_puts("[arm64] no kernel metadata\r\n");
		return;
	}

	boothowto = md_fetch_int(preload_kmdp, MODINFOMD_HOWTO);
	kern_envp = (char *)md_fetch_uintptr(preload_kmdp, MODINFOMD_ENVP);
	efi_systbl_phys = md_fetch_uintptr(preload_kmdp, MODINFOMD_FW_HANDLE);
	struct efi_map_header *efihdr =
	    (struct efi_map_header *)md_fetch_ptr(preload_kmdp, MODINFOMD_EFI_MAP);

	uart_puts("[arm64] boothowto=0x");
	uart_puthex((u_int64_t)boothowto);
	uart_puts("\r\n");
	uart_puts("[arm64] kern_envp=0x");
	uart_puthex((u_int64_t)kern_envp);
	uart_puts("\r\n");
	uart_puts("[arm64] efi_systbl=0x");
	uart_puthex((u_int64_t)efi_systbl_phys);
	uart_puts("\r\n");

	if (efihdr != NULL && efihdr->descriptor_size != 0) {
		u_int64_t count = efihdr->memory_size / efihdr->descriptor_size;
		uart_puts("[arm64] efi_map entries=");
		uart_puthex(count);
		uart_puts("\r\n");
		u_int64_t usable = 0;
		u_int64_t efisz = (sizeof(*efihdr) + 0xf) & ~0xf;
		u_int8_t *desc = (u_int8_t *)efihdr + efisz;
		for (u_int64_t i = 0; i < count; i++) {
			struct efi_md *md = (struct efi_md *)(void *)desc;
			if (md->type == EFI_MD_TYPE_CONVENTIONAL) {
				usable += md->num_pages;
				if (arm64_physmem_count < ARM64_MAX_PHYSMEM_RANGES) {
					struct arm64_phys_range *range =
					    &arm64_physmem[arm64_physmem_count++];
					range->start = md->phys_start;
					range->end = md->phys_start +
					    (md->num_pages * 4096);
				}
			}
			desc += efihdr->descriptor_size;
		}
		uart_puts("[arm64] efi_map usable_pages=");
		uart_puthex(usable);
		uart_puts("\r\n");
		uart_puts("[arm64] physmem ranges=");
		uart_puthex((u_int64_t)arm64_physmem_count);
		uart_puts("\r\n");
		for (int i = 0; i < arm64_physmem_count; i++) {
			uart_puts("[arm64] physmem[");
			uart_puthex((u_int64_t)i);
			uart_puts("] 0x");
			uart_puthex(arm64_physmem[i].start);
			uart_puts("-0x");
			uart_puthex(arm64_physmem[i].end);
			uart_puts("\r\n");
		}
		arm64_pmap_bootstrap(arm64_physmem, arm64_physmem_count);
	} else {
		uart_puts("[arm64] no efi map\r\n");
	}
}
