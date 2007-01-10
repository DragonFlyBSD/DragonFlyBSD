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
 * $DragonFly: src/sys/platform/vkernel/platform/init.c,v 1.18 2007/01/10 08:08:17 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/cons.h>
#include <sys/random.h>
#include <sys/vkernel.h>
#include <sys/tls.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/vmspace.h>
#include <vm/vm_page.h>

#include <machine/globaldata.h>
#include <machine/tls.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <assert.h>

vm_paddr_t phys_avail[16];
vm_paddr_t Maxmem;
vm_paddr_t Maxmem_bytes;
int MemImageFd = -1;
int RootImageFd = -1;
vm_offset_t KvaStart;
vm_offset_t KvaEnd;
vm_offset_t KvaSize;
vm_offset_t virtual_start;
vm_offset_t virtual_end;
vm_offset_t kernel_vm_end;
vm_offset_t crashdumpmap;
vm_offset_t clean_sva;
vm_offset_t clean_eva;
struct msgbuf *msgbufp;
caddr_t ptvmmap;
vpte_t	*KernelPTD;
vpte_t	*KernelPTA;	/* Warning: Offset for direct VA translation */
u_int cpu_feature;	/* XXX */
u_int tsc_present;	/* XXX */

struct privatespace *CPU_prvspace;

static struct trapframe proc0_tf;
static void *proc0paddr;

static void init_sys_memory(char *imageFile);
static void init_kern_memory(void);
static void init_globaldata(void);
static void init_vkernel(void);
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
	int i;
	int n;

	/*
	 * Process options
	 */
	while ((c = getopt(ac, av, "vm:r:e:")) != -1) {
		switch(c) {
		case 'e':
			/*
			 * name=value:name=value:name=value...
			 */
			n = strlen(optarg);
			kern_envp = malloc(n + 2);
			for (i = 0; i < n; ++i) {
				if (optarg[i] == ':')
					kern_envp[i] = 0;
				else
					kern_envp[i] = optarg[i];
			}
			kern_envp[i++] = 0;
			kern_envp[i++] = 0;
			break;
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
			Maxmem_bytes = strtoull(optarg, &suffix, 0);
			if (suffix) {
				switch(*suffix) {
				case 'g':
				case 'G':
					Maxmem_bytes <<= 30;
					break;
				case 'm':
				case 'M':
					Maxmem_bytes <<= 20;
					break;
				case 'k':
				case 'K':
					Maxmem_bytes <<= 10;
					break;
				default:
					Maxmem_bytes = 0;
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
	init_globaldata();
	init_vkernel();
	init_rootdevice(rootImageFile);
	init_exceptions();
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
	int i;
	int fd;

	/*
	 * Figure out the system memory image size.  If an image file was
	 * specified and -m was not specified, use the image file's size.
	 */

	if (imageFile && stat(imageFile, &st) == 0 && Maxmem_bytes == 0)
		Maxmem_bytes = (vm_paddr_t)st.st_size;
	if ((imageFile == NULL || stat(imageFile, &st) < 0) && 
	    Maxmem_bytes == 0) {
		err(1, "Cannot create new memory file %s unless "
		       "system memory size is specified with -m",
		       imageFile);
		/* NOT REACHED */
	}

	/*
	 * Maxmem must be known at this time
	 */
	if (Maxmem_bytes < 32 * 1024 * 1024 || (Maxmem_bytes & SEG_MASK)) {
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
	if (imageFile == NULL) {
		for (i = 0; i < 1000000; ++i) {
			asprintf(&imageFile, "/var/vkernel/memimg.%06d", i);
			fd = open(imageFile, 
				  O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
			if (fd < 0 && errno == EWOULDBLOCK) {
				free(imageFile);
				continue;
			}
			break;
		}
	} else {
		fd = open(imageFile, O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
	}
	printf("Using memory file: %s\n", imageFile);
	if (fd < 0 || fstat(fd, &st) < 0) {
		err(1, "Unable to open/create %s: %s",
		      imageFile, strerror(errno));
		/* NOT REACHED */
	}

	/*
	 * Truncate or extend the file as necessary.
	 */
	if (st.st_size > Maxmem_bytes) {
		ftruncate(fd, Maxmem_bytes);
	} else if (st.st_size < Maxmem_bytes) {
		char *zmem;
		off_t off = st.st_size & ~SEG_MASK;

		kprintf("%s: Reserving blocks for memory image\n", imageFile);
		zmem = malloc(SEG_SIZE);
		bzero(zmem, SEG_SIZE);
		lseek(fd, off, SEEK_SET);
		while (off < Maxmem_bytes) {
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
	Maxmem = Maxmem_bytes >> PAGE_SHIFT;
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
	char *zero;
	vpte_t pte;
	int i;

	/*
	 * Memory map our kernel virtual memory space.  Note that the
	 * kernel image itself is not made part of this memory for the
	 * moment.
	 *
	 * The memory map must be segment-aligned so we can properly
	 * offset KernelPTD.
	 */
	base = mmap((void *)0x40000000, KERNEL_KVA_SIZE, PROT_READ|PROT_WRITE,
		    MAP_FILE|MAP_SHARED|MAP_VPAGETABLE, MemImageFd, 0);
	madvise(base, KERNEL_KVA_SIZE, MADV_NOSYNC);
	if (base == MAP_FAILED) {
		err(1, "Unable to mmap() kernel virtual memory!");
		/* NOT REACHED */
	}
	KvaStart = (vm_offset_t)base;
	KvaSize = KERNEL_KVA_SIZE;
	KvaEnd = KvaStart + KvaSize;

	/*
	 * Create a top-level page table self-mapping itself. 
	 *
	 * Initialize the page directory at physical page index 0 to point
	 * to an array of page table pages starting at physical page index 1
	 */
	lseek(MemImageFd, 0L, 0);
	for (i = 0; i < KERNEL_KVA_SIZE / SEG_SIZE; ++i) {
		pte = ((i + 1) * PAGE_SIZE) | VPTE_V | VPTE_R | VPTE_W;
		write(MemImageFd, &pte, sizeof(pte));
	}

	/*
	 * Initialize the PTEs in the page table pages required to map the
	 * page table itself.  This includes mapping the page directory page
	 * at the base so we go one more loop then normal.
	 */
	lseek(MemImageFd, PAGE_SIZE, 0);
	for (i = 0; i <= KERNEL_KVA_SIZE / SEG_SIZE * sizeof(vpte_t); ++i) {
		pte = (i * PAGE_SIZE) | VPTE_V | VPTE_R | VPTE_W;
		write(MemImageFd, &pte, sizeof(pte));
	}

	/*
	 * Initialize remaining PTEs to 0.  We may be reusing a memory image
	 * file.  This is approximately a megabyte.
	 */
	i = (KERNEL_KVA_SIZE / PAGE_SIZE - i) * sizeof(pte);
	zero = malloc(PAGE_SIZE);
	while (i) {
		write(MemImageFd, zero, (i > PAGE_SIZE) ? PAGE_SIZE : i);
		i = i - ((i > PAGE_SIZE) ? PAGE_SIZE : i);
	}
	free(zero);

	/*
	 * Enable the page table and calculate pointers to our self-map
	 * for easy kernel page table manipulation.
	 *
	 * KernelPTA must be offset so we can do direct VA translations
	 */
	mcontrol(base, KERNEL_KVA_SIZE, MADV_SETMAP,
		 0 | VPTE_R | VPTE_W | VPTE_V);
	KernelPTD = (vpte_t *)base;			  /* pg directory */
	KernelPTA = (vpte_t *)((char *)base + PAGE_SIZE); /* pg table pages */
	KernelPTA -= KvaStart >> PAGE_SHIFT;

	/*
	 * phys_avail[] represents unallocated physical memory.  MI code
	 * will use phys_avail[] to create the vm_page array.
	 */
	phys_avail[0] = PAGE_SIZE +
			KERNEL_KVA_SIZE / PAGE_SIZE * sizeof(vpte_t);
	phys_avail[0] = (phys_avail[0] + PAGE_MASK) & ~(vm_paddr_t)PAGE_MASK;
	phys_avail[1] = Maxmem_bytes;

	/*
	 * (virtual_start, virtual_end) represent unallocated kernel virtual
	 * memory.  MI code will create kernel_map using these parameters.
	 */
	virtual_start = KvaStart + PAGE_SIZE +
			KERNEL_KVA_SIZE / PAGE_SIZE * sizeof(vpte_t);
	virtual_start = (virtual_start + PAGE_MASK) & ~(vm_offset_t)PAGE_MASK;
	virtual_end = KvaStart + KERNEL_KVA_SIZE;

	/*
	 * Because we just pre-allocate the entire page table the demark used
	 * to determine when KVM must be grown is just set to the end of
	 * KVM.  pmap_growkernel() simply panics.
	 */
	kernel_vm_end = virtual_end;

	/*
	 * Allocate space for process 0's UAREA.
	 */
	proc0paddr = (void *)virtual_start;
	for (i = 0; i < UPAGES; ++i) {
		pmap_kenter_quick(virtual_start, phys_avail[0]);
		virtual_start += PAGE_SIZE;
		phys_avail[0] += PAGE_SIZE;
	}

	/*
	 * crashdumpmap
	 */
	crashdumpmap = virtual_start;
	virtual_start += MAXDUMPPGS * PAGE_SIZE;

	/*
	 * msgbufp maps the system message buffer
	 */
	assert((MSGBUF_SIZE & PAGE_MASK) == 0);
	msgbufp = (void *)virtual_start;
	for (i = 0; i < (MSGBUF_SIZE >> PAGE_SHIFT); ++i) {
		pmap_kenter_quick(virtual_start, phys_avail[0]);
		virtual_start += PAGE_SIZE;
		phys_avail[0] += PAGE_SIZE;
	}
	msgbufinit(msgbufp, MSGBUF_SIZE);

	/*
	 * used by kern_memio for /dev/mem access
	 */
	ptvmmap = (caddr_t)virtual_start;
	virtual_start += PAGE_SIZE;

	/*
	 * Bootstrap the kernel_pmap
	 */
	pmap_bootstrap();
}

/*
 * Map the per-cpu globaldata for cpu #0.  Allocate the space using
 * virtual_start and phys_avail[0]
 */
static
void
init_globaldata(void)
{
	int i;
	vm_paddr_t pa;
	vm_offset_t va;

	/*
	 * Reserve enough KVA to cover possible cpus.  This is a considerable
	 * amount of KVA since the privatespace structure includes two 
	 * whole page table mappings.
	 */
	virtual_start = (virtual_start + SEG_MASK) & ~(vm_offset_t)SEG_MASK;
	CPU_prvspace = (void *)virtual_start;
	virtual_start += sizeof(struct privatespace) * SMP_MAXCPU;

	/*
	 * Allocate enough physical memory to cover the mdglobaldata
	 * portion of the space and the idle stack and map the pages
	 * into KVA.  For cpu #0 only.
	 */
	for (i = 0; i < sizeof(struct mdglobaldata); i += PAGE_SIZE) {
		pa = phys_avail[0];
		va = (vm_offset_t)&CPU_prvspace[0].mdglobaldata + i;
		pmap_kenter_quick(va, pa);
		phys_avail[0] += PAGE_SIZE;
	}
	for (i = 0; i < sizeof(CPU_prvspace[0].idlestack); i += PAGE_SIZE) {
		pa = phys_avail[0];
		va = (vm_offset_t)&CPU_prvspace[0].idlestack + i;
		pmap_kenter_quick(va, pa);
		phys_avail[0] += PAGE_SIZE;
	}

	/*
	 * Setup the %gs for cpu #0.  The mycpu macro works after this
	 * point.
	 */
	tls_set_fs(&CPU_prvspace[0], sizeof(struct privatespace));
}

/*
 * Initialize very low level systems including thread0, proc0, etc.
 */
static
void
init_vkernel(void)
{
	struct mdglobaldata *gd;

	gd = &CPU_prvspace[0].mdglobaldata;
	bzero(gd, sizeof(*gd));

	gd->mi.gd_curthread = &thread0;
	thread0.td_gd = &gd->mi;
	ncpus = 1;
	ncpus2 = 1;
	init_param1();
	gd->mi.gd_prvspace = &CPU_prvspace[0];
	mi_gdinit(&gd->mi, 0);
	cpu_gdinit(gd, 0);
	mi_proc0init(&gd->mi, proc0paddr);
	proc0.p_lwp.lwp_md.md_regs = &proc0_tf;

	/*init_locks();*/
	cninit();
	rand_initialize();
#if 0	/* #ifdef DDB */
	kdb_init();
	if (boothowto & RB_KDB)
		Debugger("Boot flags requested debugger");
#endif
#if 0
	initializecpu();	/* Initialize CPU registers */
#endif
	init_param2((phys_avail[1] - phys_avail[0]) / PAGE_SIZE);

#if 0
	/*
	 * Map the message buffer
	 */
	for (off = 0; off < round_page(MSGBUF_SIZE); off += PAGE_SIZE)
		pmap_kenter((vm_offset_t)msgbufp + off, avail_end + off);
	msgbufinit(msgbufp, MSGBUF_SIZE);
#endif
#if 0
	thread0.td_pcb_cr3 ... MMU
	proc0.p_lwp.lwp_md.md_regs = &proc0_tf;
#endif
}

/*
 * The root filesystem path for the virtual kernel is optional.  If specified
 * it points to a filesystem image.
 */
static
void
init_rootdevice(char *imageFile)
{
	struct stat st;

	if (imageFile) {
		RootImageFd = open(imageFile, O_RDWR, 0644);
		if (RootImageFd < 0 || fstat(RootImageFd, &st) < 0) {
			err(1, "Unable to open/create %s: %s",
			    imageFile, strerror(errno));
			/* NOT REACHED */
		}
		rootdevnames[0] = "ufs:vkd0a";
	}
}

static
void
usage(const char *ctl)
{
	
}

void
cpu_reset(void)
{
	kprintf("cpu reset\n");
	exit(0);
}

void
cpu_halt(void)
{
	kprintf("cpu halt\n");
	for (;;)
		__asm__ __volatile("hlt");
}
