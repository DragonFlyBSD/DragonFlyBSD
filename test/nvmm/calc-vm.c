/*
 * Copyright (c) 2018-2021 Maxime Villard, m00nbsd.net
 * All rights reserved.
 *
 * This code is part of the NVMM hypervisor.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <nvmm.h>

#define PAGE_SIZE 4096

/*
 * A simple calculator that creates a VM which performs the addition of the
 * two ints given as arguments.
 *
 * The guest does EBX+=EAX, followed by HLT. We set EAX and EBX, and then
 * fetch the result in EBX. HLT is our shutdown point, we stop the VM there.
 *
 * We give one single page to the guest, and copy there the instructions it
 * must execute. The guest runs in 16bit real mode, and its initial state is
 * the x86 RESET state (default state). The instruction pointer uses CS.base
 * as base, and this base value is 0xFFFF0000. So we make it our GPA, and set
 * RIP=0, which means "RIP=0xFFFF0000+0". The guest therefore executes the
 * instructions at GPA 0xFFFF0000.
 *
 *     $ cc -g -Wall -Wextra -o calc-vm calc-vm.c -lnvmm
 *     $ ./calc-vm 3 5
 *     Result: 8
 *
 * Don't forget to load the nvmm(4) kernel module beforehand!
 *
 * From:
 * https://www.netbsd.org/~maxv/nvmm/calc-vm.c
 * https://blog.netbsd.org/tnf/entry/from_zero_to_nvmm
 */

int main(int argc, char *argv[])
{
	const uint8_t instr[] = {
		0x01, 0xc3,	/* add %eax,%ebx */
		0xf4		/* hlt */
	};
	struct nvmm_machine mach;
	struct nvmm_vcpu vcpu;
	uintptr_t hva;
	gpaddr_t gpa = 0xFFFF0000;
	int num1, num2, ret;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <int#1> <int#2>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	num1 = atoi(argv[1]);
	num2 = atoi(argv[2]);

	/* Init NVMM. */
	if (nvmm_init() == -1)
		err(EXIT_FAILURE, "unable to init NVMM");
	printf("[+] Initialized NVMM\n");

	/* Create the VM. */
	if (nvmm_machine_create(&mach) == -1)
		err(EXIT_FAILURE, "unable to create the VM");
	printf("[+] Created machine\n");
	if (nvmm_vcpu_create(&mach, 0, &vcpu) == -1)
		err(EXIT_FAILURE, "unable to create VCPU");
	printf("[+] Created VCPU\n");

	/* Allocate a HVA. The HVA is writable. */
	hva = (uintptr_t)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_PRIVATE, -1, 0);
	if ((void *)hva == MAP_FAILED)
		err(EXIT_FAILURE, "unable to mmap");
	if (nvmm_hva_map(&mach, hva, PAGE_SIZE) == -1)
		err(EXIT_FAILURE, "unable to map HVA");
	printf("[+] Mapped HVA\n");

	/* Link the GPA towards the HVA. The GPA is executable. */
	if (nvmm_gpa_map(&mach, hva, gpa, PAGE_SIZE, PROT_READ|PROT_EXEC) == -1)
		err(EXIT_FAILURE, "unable to map GPA");
	printf("[+] Mapped GPA\n");

	/* Install the guest instructions there. */
	memcpy((void *)hva, instr, sizeof(instr));

	/* Reset the instruction pointer, and set EAX/EBX. */
	if (nvmm_vcpu_getstate(&mach, &vcpu, NVMM_X64_STATE_GPRS) == -1)
		err(EXIT_FAILURE, "unable to get VCPU state");
	printf("[+] Got VCPU states\n");
	vcpu.state->gprs[NVMM_X64_GPR_RIP] = 0;
	vcpu.state->gprs[NVMM_X64_GPR_RAX] = num1;
	vcpu.state->gprs[NVMM_X64_GPR_RBX] = num2;
	nvmm_vcpu_setstate(&mach, &vcpu, NVMM_X64_STATE_GPRS);
	printf("[+] Set VCPU states\n");

	while (1) {
		/* Run VCPU0. */
		printf("[+] Running VCPU\n");
		if (nvmm_vcpu_run(&mach, &vcpu) == -1)
			err(EXIT_FAILURE, "unable to run VCPU");
		printf("[+] VCPU exited\n");

		/* Process the exit reasons. */
		switch (vcpu.exit->reason) {
		case NVMM_VCPU_EXIT_NONE:
			/* Nothing to do, keep rolling. */
			break;
		case NVMM_VCPU_EXIT_HALTED:
			/* Our shutdown point. Fetch the result. */
			nvmm_vcpu_getstate(&mach, &vcpu, NVMM_X64_STATE_GPRS);
			ret = vcpu.state->gprs[NVMM_X64_GPR_RBX];
			printf("Result: %d\n", ret);
			return 0;
			/* THE PROCESS EXITS, THE VM GETS DESTROYED. */
		default:
			errx(EXIT_FAILURE, "unknown exit reason: 0x%lx",
			    vcpu.exit->reason);
		}
	}

	return 0;
}
