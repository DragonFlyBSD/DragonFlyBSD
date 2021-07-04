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

/*
 * This file implements two really simple devices. One is an MMIO device, the
 * other is an IO device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <nvmm.h>

#include "common.h"

/* -------------------------------------------------------------------------- */

/*
 * LAPIC device, MMIO. We only handle the ID.
 */
static void
toydev_lapic(gpaddr_t gpa, bool write, uint8_t *buf, size_t size)
{
	uint32_t *data;

#define LAPIC_BASE		0xfee00000
#define	LAPIC_ID		0x020
	if (write) {
		printf("[!] Unexpected LAPIC write!\n");
		exit(EXIT_FAILURE);
	}
	if (size != sizeof(uint32_t)) {
		printf("[!] Unexpected LAPIC read size %zu!\n", size);
		exit(EXIT_FAILURE);
	}
	if (gpa == LAPIC_BASE + LAPIC_ID) {
		data = (uint32_t *)&buf[3];
		*data = 120;
	}
}

/*
 * Console device, IO. It retrieves a string on port 123, and we display it.
 */
static int
toydev_cons(int port __unused, bool in, uint8_t *buf, size_t size)
{
	static bool new_line = true;
	size_t i;

	if (in) {
		/* This toy device doesn't take in. */
		printf("[!] Unexpected IN for the console\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < size; i++) {
		if (new_line) {
			printf("mach>\t");
			new_line = false;
		}
		printf("%c", (char)buf[i]);
		new_line = (buf[i] == '\n');
	}

	return 0;
}

/* -------------------------------------------------------------------------- */

void
toydev_mmio(gpaddr_t gpa, bool write, uint8_t *buf, size_t size)
{
	/*
	 * Dispatch MMIO requests to the proper device.
	 */
#define LAPIC_START		0xfee00000
#define LAPIC_END		0xfee01000
	if (gpa >= LAPIC_START && gpa + size <= LAPIC_END) {
		toydev_lapic(gpa, write, buf, size);
		return;
	}

	printf("[!] Unknown MMIO device GPA=%p\n", (void *)gpa);
	exit(EXIT_FAILURE);
}

void
toydev_io(int port, bool in, uint8_t *buf, size_t size)
{
	/*
	 * Dispatch IO requests to the proper device.
	 */
	if (port == 123) {
		toydev_cons(port, in, buf, size);
		return;
	}

	printf("[!] Unknown IO device PORT=%d\n", port);
	exit(EXIT_FAILURE);
}

