/* Exercise the real AMD parser and dispatch without privileged instructions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define PAGE_SIZE 4096
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(value) __attribute__((aligned(value)))
#endif
#define CPU_VENDOR_AMD 0x1022
#define CPU_VENDOR_INTEL 0x8086
#define MSR_AMD_PATCH_LEVEL 1
#define MSR_AMD_PATCH_LOADER 2
#define MODINFO_ADDR 3
#define MODINFO_SIZE 4
#define kprintf(...) ((void)0)
static int mycpuid;
static int vendor = CPU_VENDOR_AMD, writes;
static uint64_t revision = 1;
static uint32_t signature = 0x00870f10;
static void *image;
static size_t image_size;
static void do_cpuid(unsigned leaf, uint32_t *regs)
{
	memset(regs, 0, 4 * sizeof(*regs));
	if (leaf == 1) {
		regs[0] = signature;
		return;
	}
	if (vendor == CPU_VENDOR_AMD) {
		regs[1] = 0x68747541;
		regs[3] = 0x69746e65;
		regs[2] = 0x444d4163;
	} else if (vendor == CPU_VENDOR_INTEL) {
		regs[1] = 0x756e6547;
		regs[3] = 0x49656e69;
		regs[2] = 0x6c65746e;
	}
}
static uint64_t rdmsr(unsigned msr)
{
	assert(vendor == CPU_VENDOR_AMD);
	assert(msr == MSR_AMD_PATCH_LEVEL);
	return revision;
}
static void wrmsr(unsigned msr, uintptr_t address)
{
	assert(msr == MSR_AMD_PATCH_LOADER && (address & 15) == 0);
	memcpy(&revision, (const char *)address + 4, 4);
	++writes;
}
static caddr_t preload_search_by_type(const char *type)
{
	assert(!strcmp(type, "cpu_microcode"));
	return (caddr_t)image;
}
static caddr_t preload_search_info(caddr_t module, int field)
{
	return field == MODINFO_ADDR ? (caddr_t)&image : (caddr_t)&image_size;
}

/* SOURCE_UNDER_TEST */

static size_t container(uint8_t *buffer, uint32_t patch_revision,
			size_t patch_size)
{
	uint32_t magic = AMD_CONTAINER_MAGIC;
	struct amd_section_header equiv_header = {
	    AMD_SECTION_EQUIV, sizeof(struct amd_equiv_entry) * 2};
	struct amd_equiv_entry entries[2] = {
	    {.installed_cpu = signature, .equiv_id = 7}, {0}};
	struct amd_section_header patch_header = {AMD_SECTION_PATCH,
						  patch_size};
	struct amd_patch_header patch = {.patch_id = patch_revision,
					 .processor_rev_id = 7};
	size_t size = 0;
#define APPEND(value)                                                          \
	do {                                                                   \
		memcpy(buffer + size, &(value), sizeof(value));                \
		size += sizeof(value);                                         \
	} while (0)
	APPEND(magic);
	APPEND(equiv_header);
	APPEND(entries);
	APPEND(patch_header);
	memset(buffer + size, 0, patch_size);
	memcpy(buffer + size, &patch, sizeof(patch));
	return size + patch_size;
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	uint8_t buffer[16384];
	image = buffer;
	image_size = container(buffer, 2, sizeof(struct amd_patch_header));
	if (!strcmp(argv[1], "amd_update")) {
		ucode_load_bsp();
		assert(revision == 2 && writes == 1);
		ucode_apply();
		assert(writes == 1);
		revision = 1;
		mycpuid = 1;
		ucode_apply();
		assert(revision == 2 && writes == 2);
	} else if (!strcmp(argv[1], "newest")) {
		image_size += container(buffer + image_size, 4,
					sizeof(struct amd_patch_header));
		ucode_load_bsp();
		assert(revision == 4 && writes == 1);
	} else if (!strcmp(argv[1], "oversize")) {
		image_size = container(buffer, 3, PAGE_SIZE + 16);
		ucode_load_bsp();
		assert(revision == 1 && writes == 0);
	} else if (!strcmp(argv[1], "truncated")) {
		--image_size;
		ucode_load_bsp();
		assert(writes == 0);
	} else if (!strcmp(argv[1], "no_match")) {
		++signature;
		ucode_load_bsp();
		assert(writes == 0);
	} else if (!strcmp(argv[1], "intel")) {
		vendor = CPU_VENDOR_INTEL;
		ucode_load_bsp();
		assert(writes == 0);
	} else if (!strcmp(argv[1], "unknown")) {
		vendor = 0;
		ucode_load_bsp();
		assert(writes == 0);
	} else if (!strcmp(argv[1], "missing")) {
		image = NULL;
		ucode_load_bsp();
		assert(writes == 0);
	} else
		abort();
	return 0;
}
