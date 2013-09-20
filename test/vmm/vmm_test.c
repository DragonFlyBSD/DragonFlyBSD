#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmm_guest_ctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/vkernel.h>

#include <cpu/cpufunc.h>
#include <cpu/specialreg.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <stdarg.h>
#define vmm_printf(val, err, exp) \
	printf("vmm_guest(%d): return %d, expected %d\n", val, err, exp);

#define STACK_SIZE (512 * PAGE_SIZE)

int
vmm_boostrap(void)
{
	struct guest_options options;
	uint64_t *ptr;
	uint64_t stack_source;
	uint64_t stack_size;
	uint64_t stack_dest;
	int pml4_stack_index;
	int pdp_stack_index;
	int pd_stack_index;
	char tst[1024];
	void *stack;
	uint64_t i,j;
	int regs[4];
	int amd_feature;

	stack = mmap(NULL, STACK_SIZE,
	    PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_ANON, -1, 0);

	if (stack == MAP_FAILED) {
		printf("Error on allocating stack\n");
		return -1;
	}

	posix_memalign((void **) &ptr, PAGE_SIZE, (512 + 4) * PAGE_SIZE);
	bzero(ptr, (512 + 4) * PAGE_SIZE);

	uint64_t *pml4 = ptr;
	uint64_t *pdp = (uint64_t *)((uint64_t)ptr + PAGE_SIZE);
	uint64_t *pdp_stack = (uint64_t *)((uint64_t)ptr + 2 * PAGE_SIZE);
	uint64_t *pd_stack = (uint64_t *)((uint64_t)ptr + 3 * PAGE_SIZE);
	uint64_t *pd_vec = (uint64_t *)((uint64_t)ptr + 4 * PAGE_SIZE);

	pml4[0] = (uint64_t) pdp | VPTE_V | VPTE_RW| VPTE_U;

	do_cpuid(0x80000001, regs);
	amd_feature = regs[3];

	if (amd_feature & AMDID_PAGE1GB) {
		for (i = 0; i < VPTE_PAGE_ENTRIES; i++) {
			pdp[i] = i << 30;
			pdp[i] |=  VPTE_V | VPTE_RW | VPTE_U;
		}
	} else {
		for (i = 0; i < VPTE_PAGE_ENTRIES; i++) {
			uint64_t *pd = &pd_vec[i * VPTE_PAGE_ENTRIES];
			pdp[i] = (uint64_t) pd;
			pdp[i] |=  VPTE_V | VPTE_RW | VPTE_U;
			for (j = 0; j < VPTE_PAGE_ENTRIES; j++) {
				pd[j] = (i << 30) | (j << 21);
				pd[j] |=  VPTE_V | VPTE_RW | VPTE_U | VPTE_PS;
			}
		}
	}

	void *stack_addr = NULL;

	pml4_stack_index = (uint64_t)&stack_addr >> PML4SHIFT;
	pml4[pml4_stack_index] = (uint64_t) pdp_stack;
	pml4[pml4_stack_index] |= VPTE_V | VPTE_RW| VPTE_U;

	pdp_stack_index = ((uint64_t)&stack_addr & PML4MASK) >> PDPSHIFT;
	pdp_stack[pdp_stack_index] = (uint64_t) pd_stack;
	pdp_stack[pdp_stack_index] |= VPTE_V | VPTE_RW| VPTE_U;

	pd_stack_index = ((uint64_t)&stack_addr & PDPMASK) >> PDRSHIFT;
	pd_stack[pd_stack_index] = (uint64_t) stack;
	pd_stack[pd_stack_index] |= VPTE_V | VPTE_RW| VPTE_U | VPTE_PS;

	options.new_stack = (uint64_t)stack + STACK_SIZE;
	options.guest_cr3 = (register_t) pml4;
	options.master = 1;
	if(vmm_guest_ctl(VMM_GUEST_RUN, &options)) {
		printf("Error: VMM enter failed\n");
	}
	printf("vmm_bootstrap: VMM bootstrap success\n");

	return 0;
}
int
main(void)
{
	vmm_boostrap();
	printf("vmm_test: VMM bootstrap success\n");
	return 0;
}
