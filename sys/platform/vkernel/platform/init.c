/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/platform/vkernel/platform/init.c,v 1.2 2006/12/05 23:14:54 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <vm/vm_page.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>

vm_paddr_t Maxmem;
int MemImageFd = -1;
int RootImageFd = -1;
vm_offset_t KvaBase;
vm_offset_t KvaSize;

static void init_sys_memory(char *imageFile);
static void init_kern_memory(void);
static void init_rootdevice(char *imageFile);
static void usage(const char *ctl);

/*
 * Kernel startup for virtual kernels - standard main() 
 */
int
main(int ac, char **av)
{
	char *memImageFile = NULL;
	char *rootImageFile = NULL;
	char *suffix;
	int c;

	/*
	 * Process options
	 */
	while ((c = getopt(ac, av, "vm:")) != -1) {
		switch(c) {
		case 'v':
			bootverbose = 1;
			break;
		case 'i':
			memImageFile = optarg;
			break;
		case 'r':
			rootImageFile = optarg;
			break;
		case 'm':
			Maxmem = strtoull(optarg, &suffix, 0);
			if (suffix) {
				switch(*suffix) {
				case 'g':
				case 'G':
					Maxmem <<= 30;
					break;
				case 'm':
				case 'M':
					Maxmem <<= 20;
					break;
				case 'k':
				case 'K':
					Maxmem <<= 10;
					break;
				default:
					Maxmem = 0;
					usage("Bad maxmem option");
					/* NOT REACHED */
					break;
				}
			}
			break;
		}
	}

	init_sys_memory(memImageFile);
	init_kern_memory();
	init_rootdevice(rootImageFile);
	mi_startup();
	/* NOT REACHED */
	exit(1);
}

/*
 * Initialize system memory.  This is the virtual kernel's 'RAM'.
 */
static
void
init_sys_memory(char *imageFile)
{
	struct stat st;
	int fd;

	/*
	 * Figure out the system memory image size.  If an image file was
	 * specified and -m was not specified, use the image file's size.
	 */

	if (imageFile && stat(imageFile, &st) == 0 && Maxmem == 0)
		Maxmem = (vm_paddr_t)st.st_size;
	if ((imageFile == NULL || stat(imageFile, &st) < 0) && Maxmem == 0) {
		err(1, "Cannot create new memory file %s unless "
		       "system memory size is specified with -m",
		       imageFile);
		/* NOT REACHED */
	}

	/*
	 * Maxmem must be known at this time
	 */
	if (Maxmem < 32 * 1024 * 1024 || (Maxmem & SEG_MASK)) {
		err(1, "Bad maxmem specification: 32MB minimum, "
		       "multiples of %dMB only",
		       SEG_SIZE / 1024 / 1024);
		/* NOT REACHED */
	}

	/*
	 * Generate an image file name if necessary, then open/create the
	 * file exclusively locked.  Do not allow multiple virtual kernels
	 * to use the same image file.
	 */
	if (imageFile == NULL)
		asprintf(&imageFile, "/var/vkernel/image.%05d", (int)getpid());
	fd = open(imageFile, O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
	if (fd < 0 || fstat(fd, &st) < 0) {
		err(1, "Unable to open/create %s: %s",
		      imageFile, strerror(errno));
		/* NOT REACHED */
	}

	/*
	 * Truncate or extend the file as necessary.
	 */
	if (st.st_size > Maxmem) {
		ftruncate(fd, Maxmem);
	} else if (st.st_size < Maxmem) {
		char *zmem;
		off_t off = st.st_size & ~SEG_MASK;

		printf("%s: Reserving blocks for memory image\n", imageFile);
		zmem = malloc(SEG_SIZE);
		bzero(zmem, SEG_SIZE);
		lseek(fd, off, 0);
		while (off < Maxmem) {
			if (write(fd, zmem, SEG_SIZE) != SEG_SIZE) {
				err(1, "Unable to reserve blocks for memory image");
				/* NOT REACHED */
			}
			off += SEG_SIZE;
		}
		if (fsync(fd) < 0)
			err(1, "Unable to reserve blocks for memory image");
		free(zmem);
	}
	MemImageFd = fd;
}

/*
 * Initialize kernel memory.  This reserves kernel virtual memory by using
 * MAP_VPAGETABLE
 */
static
void
init_kern_memory(void)
{
	void *base;

	/*
	 * Memory map our kernel virtual memory space.  Note that the
	 * kernel image itself is not made part of this memory for the
	 * moment.
	 */
	base = mmap(NULL, KERNEL_KVA_SIZE, PROT_READ|PROT_WRITE,
		    MAP_FILE|MAP_VPAGETABLE, MemImageFd, 0);
	if (base == MAP_FAILED) {
		err(1, "Unable to mmap() kernel virtual memory!");
		/* NOT REACHED */
	}
	KvaBase = (vm_offset_t)base;
	KvaSize = KERNEL_KVA_SIZE;

	/*
	 * Create a top-level page table self-mapping itself.
	 */
}

/*
 * The root filesystem path for the virtual kernel is optional.  If specified
 * it points to a filesystem image.
 */
static
void
init_rootdevice(char *imageFile)
{
}

static
void
usage(const char *ctl)
{
	
}

