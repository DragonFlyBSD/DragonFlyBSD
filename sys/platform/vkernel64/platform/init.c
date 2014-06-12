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
#include <sys/reboot.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/vmspace.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <sys/mplock2.h>
#include <sys/wait.h>
#include <sys/vmm.h>

#include <machine/cpu.h>
#include <machine/globaldata.h>
#include <machine/tls.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>
#include <cpu/specialreg.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/bridge/if_bridgevar.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_var.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <assert.h>
#include <sysexits.h>

#define EX_VKERNEL_REBOOT	32

vm_paddr_t phys_avail[16];
vm_paddr_t Maxmem;
vm_paddr_t Maxmem_bytes;
long physmem;
int MemImageFd = -1;
struct vkdisk_info DiskInfo[VKDISK_MAX];
int DiskNum;
struct vknetif_info NetifInfo[VKNETIF_MAX];
int NetifNum;
char *pid_file;
vm_offset_t KvaStart;
vm_offset_t KvaEnd;
vm_offset_t KvaSize;
vm_offset_t virtual_start;
vm_offset_t virtual_end;
vm_offset_t virtual2_start;
vm_offset_t virtual2_end;
vm_offset_t kernel_vm_end;
vm_offset_t crashdumpmap;
vm_offset_t clean_sva;
vm_offset_t clean_eva;
struct msgbuf *msgbufp;
caddr_t ptvmmap;
vpte_t	*KernelPTD;
vpte_t	*KernelPTA;	/* Warning: Offset for direct VA translation */
void *dmap_min_address;
void *vkernel_stack;
u_int cpu_feature;	/* XXX */
int tsc_present;
int tsc_invariant;
int tsc_mpsync;
int64_t tsc_frequency;
int optcpus;		/* number of cpus - see mp_start() */
int lwp_cpu_lock;	/* if/how to lock virtual CPUs to real CPUs */
int real_ncpus;		/* number of real CPUs */
int next_cpu;		/* next real CPU to lock a virtual CPU to */
int vkernel_b_arg;	/* no of logical CPU bits - only SMP */
int vkernel_B_arg;	/* no of core bits - only SMP */
int vmm_enabled;	/* VMM HW assisted enable */
struct privatespace *CPU_prvspace;

extern uint64_t KPML4phys;	/* phys addr of kernel level 4 */

static struct trapframe proc0_tf;
static void *proc0paddr;

static void init_sys_memory(char *imageFile);
static void init_kern_memory(void);
static void init_kern_memory_vmm(void);
static void init_globaldata(void);
static void init_vkernel(void);
static void init_disk(char *diskExp[], int diskFileNum, enum vkdisk_type type);
static void init_netif(char *netifExp[], int netifFileNum);
static void writepid(void);
static void cleanpid(void);
static int unix_connect(const char *path);
static void usage_err(const char *ctl, ...);
static void usage_help(_Bool);
static void init_locks(void);

static int save_ac;
static char **save_av;

/*
 * Kernel startup for virtual kernels - standard main()
 */
int main(int ac, char **av) {
	char *memImageFile = NULL;
	char *netifFile[VKNETIF_MAX];
	char *diskFile[VKDISK_MAX];
	char *cdFile[VKDISK_MAX];
	char *suffix;
	char *endp;
	char *tmp;
	char *tok;
	int netifFileNum = 0;
	int diskFileNum = 0;
	int cdFileNum = 0;
	int bootOnDisk = -1;	/* set below to vcd (0) or vkd (1) */
	int c;
	int i;
	int j;
	int n;
	int isq;
	int pos;
	int eflag;
	int dflag = 0;		/* disable vmm */
	int real_vkernel_enable;
	int supports_sse;
	uint32_t mxcsr_mask;
	size_t vsize;
	size_t msize;
	size_t kenv_size;
	size_t kenv_size2;
	pid_t pid;
	int status;
	struct sigaction sa;

	/*
	 * Currently a bad hack but rtld-elf needs LD_SHAREDLIB_BASE to
	 * be set to force it to mmap() shared libraries into low memory,
	 * so our module loader can link against the related symbols.
	 */
	if (getenv("LD_SHAREDLIB_BASE") == NULL) {
		setenv("LD_SHAREDLIB_BASE", "0x10000000", 1);
		execv(av[0], av);
		fprintf(stderr, "Must run %s with full path\n", av[0]);
		exit(1);
	}

	while ((pid = fork()) != 0) {
		/* Ignore signals */
		bzero(&sa, sizeof(sa));
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = SIG_IGN;
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGHUP, &sa, NULL);

		/*
		 * Wait for child to terminate, exit if
		 * someone stole our child.
		 */
		while (waitpid(pid, &status, 0) != pid) {
			if (errno == ECHILD)
				exit(1);
		}
		if (WEXITSTATUS(status) != EX_VKERNEL_REBOOT)
			return 0;
	}

	/*
	 * Starting for real
	 */
	save_ac = ac;
	save_av = av;
	eflag = 0;
	pos = 0;
	kenv_size = 0;
	/*
	 * Process options
	 */
	kernel_mem_readonly = 1;
	optcpus = 2;
	vkernel_b_arg = 0;
	vkernel_B_arg = 0;
	lwp_cpu_lock = LCL_NONE;

	real_vkernel_enable = 0;
	vsize = sizeof(real_vkernel_enable);
	sysctlbyname("vm.vkernel_enable", &real_vkernel_enable, &vsize, NULL,0);

	if (real_vkernel_enable == 0) {
		errx(1, "vm.vkernel_enable is 0, must be set "
			"to 1 to execute a vkernel!");
	}

	real_ncpus = 1;
	vsize = sizeof(real_ncpus);
	sysctlbyname("hw.ncpu", &real_ncpus, &vsize, NULL, 0);

	if (ac < 2)
		usage_help(false);

	while ((c = getopt(ac, av, "c:hsvl:m:n:r:e:i:p:I:Ud")) != -1) {
		switch(c) {
		case 'd':
			dflag = 1;
			break;
		case 'e':
			/*
			 * name=value:name=value:name=value...
			 * name="value"...
			 *
			 * Allow values to be quoted but note that shells
			 * may remove the quotes, so using this feature
			 * to embed colons may require a backslash.
			 */
			n = strlen(optarg);
			isq = 0;

			if (eflag == 0) {
				kenv_size = n + 2;
				kern_envp = malloc(kenv_size);
				if (kern_envp == NULL)
					errx(1, "Couldn't allocate %zd bytes for kern_envp", kenv_size);
			} else {
				kenv_size2 = kenv_size + n + 1;
				pos = kenv_size - 1;
				if ((tmp = realloc(kern_envp, kenv_size2)) == NULL)
					errx(1, "Couldn't reallocate %zd bytes for kern_envp", kenv_size2);
				kern_envp = tmp;
				kenv_size = kenv_size2;
			}

			for (i = 0, j = pos; i < n; ++i) {
				if (optarg[i] == '"')
					isq ^= 1;
				else if (optarg[i] == '\'')
					isq ^= 2;
				else if (isq == 0 && optarg[i] == ':')
					kern_envp[j++] = 0;
				else
					kern_envp[j++] = optarg[i];
			}
			kern_envp[j++] = 0;
			kern_envp[j++] = 0;
			eflag++;
			break;
		case 's':
			boothowto |= RB_SINGLE;
			break;
		case 'v':
			bootverbose = 1;
			break;
		case 'i':
			memImageFile = optarg;
			break;
		case 'I':
			if (netifFileNum < VKNETIF_MAX)
				netifFile[netifFileNum++] = strdup(optarg);
			break;
		case 'r':
			if (bootOnDisk < 0)
				bootOnDisk = 1;
			if (diskFileNum + cdFileNum < VKDISK_MAX)
				diskFile[diskFileNum++] = strdup(optarg);
			break;
		case 'c':
			if (bootOnDisk < 0)
				bootOnDisk = 0;
			if (diskFileNum + cdFileNum < VKDISK_MAX)
				cdFile[cdFileNum++] = strdup(optarg);
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
					usage_err("Bad maxmem option");
					/* NOT REACHED */
					break;
				}
			}
			break;
		case 'l':
			next_cpu = -1;
			if (strncmp("map", optarg, 3) == 0) {
				lwp_cpu_lock = LCL_PER_CPU;
				if (optarg[3] == ',') {
					next_cpu = strtol(optarg+4, &endp, 0);
					if (*endp != '\0')
						usage_err("Bad target CPU number at '%s'", endp);
				} else {
					next_cpu = 0;
				}
				if (next_cpu < 0 || next_cpu > real_ncpus - 1)
					usage_err("Bad target CPU, valid range is 0-%d", real_ncpus - 1);
			} else if (strncmp("any", optarg, 3) == 0) {
				lwp_cpu_lock = LCL_NONE;
			} else {
				lwp_cpu_lock = LCL_SINGLE_CPU;
				next_cpu = strtol(optarg, &endp, 0);
				if (*endp != '\0')
					usage_err("Bad target CPU number at '%s'", endp);
				if (next_cpu < 0 || next_cpu > real_ncpus - 1)
					usage_err("Bad target CPU, valid range is 0-%d", real_ncpus - 1);
			}
			break;
		case 'n':
			/*
			 * This value is set up by mp_start(), don't just
			 * set ncpus here.
			 */
			tok = strtok(optarg, ":");
			optcpus = strtol(tok, NULL, 0);
			if (optcpus < 1 || optcpus > MAXCPU)
				usage_err("Bad ncpus, valid range is 1-%d", MAXCPU);

			/* :lbits argument */
			tok = strtok(NULL, ":");
			if (tok != NULL) {
				vkernel_b_arg = strtol(tok, NULL, 0);

				/* :cbits argument */
				tok = strtok(NULL, ":");
				if (tok != NULL) {
					vkernel_B_arg = strtol(tok, NULL, 0);
				}

			}
			break;
		case 'p':
			pid_file = optarg;
			break;
		case 'U':
			kernel_mem_readonly = 0;
			break;
		case 'h':
			usage_help(true);
			break;
		default:
			usage_help(false);
		}
	}

	/*
	 * Check VMM presence
	 */
	vsize = sizeof(vmm_enabled);
	sysctlbyname("hw.vmm.enable", &vmm_enabled, &vsize, NULL, 0);
	vmm_enabled = (vmm_enabled && !dflag);

	writepid();
	cpu_disable_intr();
	if (vmm_enabled) {
		/* use a MAP_ANON directly */
		init_kern_memory_vmm();
	} else {
		init_sys_memory(memImageFile);
		init_kern_memory();
	}
	init_globaldata();
	init_vkernel();
	setrealcpu();
	init_kqueue();

	vmm_guest = VMM_GUEST_VKERNEL;

	/*
	 * Check TSC
	 */
	vsize = sizeof(tsc_present);
	sysctlbyname("hw.tsc_present", &tsc_present, &vsize, NULL, 0);
	vsize = sizeof(tsc_invariant);
	sysctlbyname("hw.tsc_invariant", &tsc_invariant, &vsize, NULL, 0);
	vsize = sizeof(tsc_mpsync);
	sysctlbyname("hw.tsc_mpsync", &tsc_mpsync, &vsize, NULL, 0);
	vsize = sizeof(tsc_frequency);
	sysctlbyname("hw.tsc_frequency", &tsc_frequency, &vsize, NULL, 0);
	if (tsc_present)
		cpu_feature |= CPUID_TSC;

	/*
	 * Check SSE
	 */
	vsize = sizeof(supports_sse);
	supports_sse = 0;
	sysctlbyname("hw.instruction_sse", &supports_sse, &vsize, NULL, 0);
	sysctlbyname("hw.mxcsr_mask", &mxcsr_mask, &msize, NULL, 0);
	init_fpu(supports_sse);
	if (supports_sse)
		cpu_feature |= CPUID_SSE | CPUID_FXSR;

	/*
	 * We boot from the first installed disk.
	 */
	if (bootOnDisk == 1) {
		init_disk(diskFile, diskFileNum, VKD_DISK);
		init_disk(cdFile, cdFileNum, VKD_CD);
	} else {
		init_disk(cdFile, cdFileNum, VKD_CD);
		init_disk(diskFile, diskFileNum, VKD_DISK);
	}

	init_netif(netifFile, netifFileNum);
	init_exceptions();
	mi_startup();
	/* NOT REACHED */
	exit(EX_SOFTWARE);
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
		errx(1, "Cannot create new memory file %s unless "
		       "system memory size is specified with -m",
		       imageFile);
		/* NOT REACHED */
	}

	/*
	 * Maxmem must be known at this time
	 */
	if (Maxmem_bytes < 64 * 1024 * 1024 || (Maxmem_bytes & SEG_MASK)) {
		errx(1, "Bad maxmem specification: 64MB minimum, "
		       "multiples of %dMB only",
		       SEG_SIZE / 1024 / 1024);
		/* NOT REACHED */
	}

	/*
	 * Generate an image file name if necessary, then open/create the
	 * file exclusively locked.  Do not allow multiple virtual kernels
	 * to use the same image file.
	 *
	 * Don't iterate through a million files if we do not have write
	 * access to the directory, stop if our open() failed on a
	 * non-existant file.  Otherwise opens can fail for any number
	 */
	if (imageFile == NULL) {
		for (i = 0; i < 1000000; ++i) {
			asprintf(&imageFile, "/var/vkernel/memimg.%06d", i);
			fd = open(imageFile,
				  O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
			if (fd < 0 && stat(imageFile, &st) == 0) {
				free(imageFile);
				continue;
			}
			break;
		}
	} else {
		fd = open(imageFile, O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0644);
	}
	fprintf(stderr, "Using memory file: %s\n", imageFile);
	if (fd < 0 || fstat(fd, &st) < 0) {
		err(1, "Unable to open/create %s", imageFile);
		/* NOT REACHED */
	}

	/*
	 * Truncate or extend the file as necessary.  Clean out the contents
	 * of the file, we want it to be full of holes so we don't waste
	 * time reading in data from an old file that we no longer care
	 * about.
	 */
	ftruncate(fd, 0);
	ftruncate(fd, Maxmem_bytes);

	MemImageFd = fd;
	Maxmem = Maxmem_bytes >> PAGE_SHIFT;
	physmem = Maxmem;
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
	int i;
	void *firstfree;

	/*
	 * Memory map our kernel virtual memory space.  Note that the
	 * kernel image itself is not made part of this memory for the
	 * moment.
	 *
	 * The memory map must be segment-aligned so we can properly
	 * offset KernelPTD.
	 *
	 * If the system kernel has a different MAXDSIZ, it might not
	 * be possible to map kernel memory in its prefered location.
	 * Try a number of different locations.
	 */

	base = mmap((void*)KERNEL_KVA_START, KERNEL_KVA_SIZE, PROT_READ|PROT_WRITE,
		    MAP_FILE|MAP_SHARED|MAP_VPAGETABLE|MAP_FIXED|MAP_TRYFIXED,
		    MemImageFd, (off_t)KERNEL_KVA_START);

	if (base == MAP_FAILED) {
		err(1, "Unable to mmap() kernel virtual memory!");
		/* NOT REACHED */
	}
	madvise(base, KERNEL_KVA_SIZE, MADV_NOSYNC);
	KvaStart = (vm_offset_t)base;
	KvaSize = KERNEL_KVA_SIZE;
	KvaEnd = KvaStart + KvaSize;

	/* cannot use kprintf yet */
	printf("KVM mapped at %p-%p\n", (void *)KvaStart, (void *)KvaEnd);

	/* MAP_FILE? */
	dmap_min_address = mmap(0, DMAP_SIZE, PROT_READ|PROT_WRITE,
				MAP_NOCORE|MAP_NOSYNC|MAP_SHARED,
				MemImageFd, 0);
	if (dmap_min_address == MAP_FAILED) {
		err(1, "Unable to mmap() kernel DMAP region!");
		/* NOT REACHED */
	}

	/*
	 * Bootstrap the kernel_pmap
	 */
	firstfree = NULL;
	pmap_bootstrap((vm_paddr_t *)&firstfree, (int64_t)base);

	mcontrol(base, KERNEL_KVA_SIZE, MADV_SETMAP,
		 0 | VPTE_RW | VPTE_V);

	/*
	 * phys_avail[] represents unallocated physical memory.  MI code
	 * will use phys_avail[] to create the vm_page array.
	 */
	phys_avail[0] = (vm_paddr_t)firstfree;
	phys_avail[0] = (phys_avail[0] + PAGE_MASK) & ~(vm_paddr_t)PAGE_MASK;
	phys_avail[1] = Maxmem_bytes;

#if JGV
	/*
	 * (virtual_start, virtual_end) represent unallocated kernel virtual
	 * memory.  MI code will create kernel_map using these parameters.
	 */
	virtual_start = KvaStart + (long)firstfree;
	virtual_start = (virtual_start + PAGE_MASK) & ~(vm_offset_t)PAGE_MASK;
	virtual_end = KvaStart + KERNEL_KVA_SIZE;
#endif

	/*
	 * pmap_growkernel() will set the correct value.
	 */
	kernel_vm_end = 0;

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
}

static
void
init_kern_memory_vmm(void)
{
	int i;
	void *firstfree;
	struct vmm_guest_options options;
	void *dmap_address;

	KvaStart = (vm_offset_t)KERNEL_KVA_START;
	KvaSize = KERNEL_KVA_SIZE;
	KvaEnd = KvaStart + KvaSize;

	Maxmem = Maxmem_bytes >> PAGE_SHIFT;
	physmem = Maxmem;

	if (Maxmem_bytes < 64 * 1024 * 1024 || (Maxmem_bytes & SEG_MASK)) {
		errx(1, "Bad maxmem specification: 64MB minimum, "
		       "multiples of %dMB only",
		       SEG_SIZE / 1024 / 1024);
		/* NOT REACHED */
	}

	/* Call the vmspace_create to allocate the internal
	 * vkernel structures. Won't do anything else (no new
	 * vmspace)
	 */
	if (vmspace_create(NULL, 0, NULL) < 0)
		panic("vmspace_create() failed");


	/*
	 * MAP_ANON the region of the VKERNEL phyisical memory
	 * (known as GPA - Guest Physical Address
	 */
	dmap_address = mmap(NULL, Maxmem_bytes, PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_ANON|MAP_SHARED, -1, 0);
	if (dmap_address == MAP_FAILED) {
		err(1, "Unable to mmap() RAM region!");
		/* NOT REACHED */
	}

	/* Alloc a new stack in the lowmem */
	vkernel_stack = mmap(NULL, KERNEL_STACK_SIZE,
	    PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_ANON, -1, 0);
	if (vkernel_stack == MAP_FAILED) {
		err(1, "Unable to allocate stack\n");
	}

	/*
	 * Bootstrap the kernel_pmap
	 */
	firstfree = dmap_address;
	dmap_min_address = NULL; /* VIRT == PHYS in the first 512G */
	pmap_bootstrap((vm_paddr_t *)&firstfree, (uint64_t)KvaStart);

	/*
	 * Enter VMM mode
	 */
	options.guest_cr3 = (register_t) KPML4phys;
	options.new_stack = (uint64_t) vkernel_stack + KERNEL_STACK_SIZE;
	options.master = 1;
	if (vmm_guest_ctl(VMM_GUEST_RUN, &options)) {
		err(1, "Unable to enter VMM mode.");
	}

	/*
	 * phys_avail[] represents unallocated physical memory.  MI code
	 * will use phys_avail[] to create the vm_page array.
	 */
	phys_avail[0] = (vm_paddr_t)firstfree;
	phys_avail[0] = (phys_avail[0] + PAGE_MASK) & ~(vm_paddr_t)PAGE_MASK;
	phys_avail[1] = (vm_paddr_t)dmap_address + Maxmem_bytes;

	/*
	 * pmap_growkernel() will set the correct value.
	 */
	kernel_vm_end = 0;

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

	printf("vmm: Hardware pagetable enabled for guest\n");
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
	 * point.  Note that %fs is used by pthreads.
	 */
	tls_set_gs(&CPU_prvspace[0], sizeof(struct privatespace));
}


/*
 * Initialize pool tokens and other necessary locks
 */
static void
init_locks(void)
{

        /*
         * Get the initial mplock with a count of 1 for the BSP.
         * This uses a LOGICAL cpu ID, ie BSP == 0.
         */
        cpu_get_initial_mplock();

        /* our token pool needs to work early */
        lwkt_token_pool_init();

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
	ncpus2 = 1;	/* rounded down power of 2 */
	ncpus_fit = 1;	/* rounded up power of 2 */
	/* ncpus2_mask and ncpus_fit_mask are 0 */
	init_param1();
	gd->mi.gd_prvspace = &CPU_prvspace[0];
	mi_gdinit(&gd->mi, 0);
	cpu_gdinit(gd, 0);
	mi_proc0init(&gd->mi, proc0paddr);
	lwp0.lwp_md.md_regs = &proc0_tf;

	init_locks();
	cninit();
	rand_initialize();
#if 0	/* #ifdef DDB */
	kdb_init();
	if (boothowto & RB_KDB)
		Debugger("Boot flags requested debugger");
#endif
	identcpu();
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
	lwp0.lwp_md.md_regs = &proc0_tf;
#endif
}

/*
 * Filesystem image paths for the virtual kernel are optional.
 * If specified they each should point to a disk image,
 * the first of which will become the root disk.
 *
 * The virtual kernel caches data from our 'disk' just like a normal kernel,
 * so we do not really want the real kernel to cache the data too.  Use
 * O_DIRECT to remove the duplication.
 */
static
void
init_disk(char *diskExp[], int diskFileNum, enum vkdisk_type type)
{
	char *serno;
	int i;

        if (diskFileNum == 0)
                return;

	for(i=0; i < diskFileNum; i++){
		char *fname;
		fname = diskExp[i];

		if (fname == NULL) {
                        warnx("Invalid argument to '-r'");
                        continue;
                }
		/*
		 * Check for a serial number for the virtual disk
		 * passed from the command line.
		 */
		serno = fname;
		strsep(&serno, ":");

		if (DiskNum < VKDISK_MAX) {
			struct stat st;
			struct vkdisk_info* info = NULL;
			int fd;
			size_t l = 0;

			if (type == VKD_DISK)
			    fd = open(fname, O_RDWR|O_DIRECT, 0644);
			else
			    fd = open(fname, O_RDONLY|O_DIRECT, 0644);
			if (fd < 0 || fstat(fd, &st) < 0) {
				err(1, "Unable to open/create %s", fname);
				/* NOT REACHED */
			}
			if (S_ISREG(st.st_mode)) {
				if (flock(fd, LOCK_EX|LOCK_NB) < 0) {
					errx(1, "Disk image %s is already "
						"in use\n", fname);
					/* NOT REACHED */
				}
			}

			info = &DiskInfo[DiskNum];
			l = strlen(fname);

			info->unit = i;
			info->fd = fd;
			info->type = type;
			memcpy(info->fname, fname, l);
			info->serno = NULL;
			if (serno) {
				if ((info->serno = malloc(SERNOLEN)) != NULL)
					strlcpy(info->serno, serno, SERNOLEN);
				else
					warnx("Couldn't allocate memory for the operation");
			}

			if (DiskNum == 0) {
				if (type == VKD_CD) {
				    rootdevnames[0] = "cd9660:vcd0";
				} else if (type == VKD_DISK) {
				    rootdevnames[0] = "ufs:vkd0s0a";
				    rootdevnames[1] = "ufs:vkd0s1a";
				}
			}

			DiskNum++;
		} else {
                        warnx("vkd%d (%s) > VKDISK_MAX", DiskNum, fname);
                        continue;
		}
	}
}

static
int
netif_set_tapflags(int tap_unit, int f, int s)
{
	struct ifreq ifr;
	int flags;

	bzero(&ifr, sizeof(ifr));

	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "tap%d", tap_unit);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		warn("tap%d: ioctl(SIOCGIFFLAGS) failed", tap_unit);
		return -1;
	}

	/*
	 * Adjust if_flags
	 *
	 * If the flags are already set/cleared, then we return
	 * immediately to avoid extra syscalls
	 */
	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	if (f < 0) {
		/* Turn off flags */
		f = -f;
		if ((flags & f) == 0)
			return 0;
		flags &= ~f;
	} else {
		/* Turn on flags */
		if (flags & f)
			return 0;
		flags |= f;
	}

	/*
	 * Fix up ifreq.ifr_name, since it may be trashed
	 * in previous ioctl(SIOCGIFFLAGS)
	 */
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "tap%d", tap_unit);

	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
		warn("tap%d: ioctl(SIOCSIFFLAGS) failed", tap_unit);
		return -1;
	}
	return 0;
}

static
int
netif_set_tapaddr(int tap_unit, in_addr_t addr, in_addr_t mask, int s)
{
	struct ifaliasreq ifra;
	struct sockaddr_in *in;

	bzero(&ifra, sizeof(ifra));
	snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "tap%d", tap_unit);

	/* Setup address */
	in = (struct sockaddr_in *)&ifra.ifra_addr;
	in->sin_family = AF_INET;
	in->sin_len = sizeof(*in);
	in->sin_addr.s_addr = addr;

	if (mask != 0) {
		/* Setup netmask */
		in = (struct sockaddr_in *)&ifra.ifra_mask;
		in->sin_len = sizeof(*in);
		in->sin_addr.s_addr = mask;
	}

	if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
		warn("tap%d: ioctl(SIOCAIFADDR) failed", tap_unit);
		return -1;
	}
	return 0;
}

static
int
netif_add_tap2brg(int tap_unit, const char *ifbridge, int s)
{
	struct ifbreq ifbr;
	struct ifdrv ifd;

	bzero(&ifbr, sizeof(ifbr));
	snprintf(ifbr.ifbr_ifsname, sizeof(ifbr.ifbr_ifsname),
		 "tap%d", tap_unit);

	bzero(&ifd, sizeof(ifd));
	strlcpy(ifd.ifd_name, ifbridge, sizeof(ifd.ifd_name));
	ifd.ifd_cmd = BRDGADD;
	ifd.ifd_len = sizeof(ifbr);
	ifd.ifd_data = &ifbr;

	if (ioctl(s, SIOCSDRVSPEC, &ifd) < 0) {
		/*
		 * 'errno == EEXIST' means that the tap(4) is already
		 * a member of the bridge(4)
		 */
		if (errno != EEXIST) {
			warn("ioctl(%s, SIOCSDRVSPEC) failed", ifbridge);
			return -1;
		}
	}
	return 0;
}

#define TAPDEV_OFLAGS	(O_RDWR | O_NONBLOCK)

/*
 * Locate the first unused tap(4) device file if auto mode is requested,
 * or open the user supplied device file, and bring up the corresponding
 * tap(4) interface.
 *
 * NOTE: Only tap(4) device file is supported currently
 */
static
int
netif_open_tap(const char *netif, int *tap_unit, int s)
{
	char tap_dev[MAXPATHLEN];
	int tap_fd, failed;
	struct stat st;
	char *dname;

	*tap_unit = -1;

	if (strcmp(netif, "auto") == 0) {
		/*
		 * Find first unused tap(4) device file
		 */
		tap_fd = open("/dev/tap", TAPDEV_OFLAGS);
		if (tap_fd < 0) {
			warnc(errno, "Unable to find a free tap(4)");
			return -1;
		}
	} else {
		/*
		 * User supplied tap(4) device file or unix socket.
		 */
		if (netif[0] == '/')	/* Absolute path */
			strlcpy(tap_dev, netif, sizeof(tap_dev));
		else
			snprintf(tap_dev, sizeof(tap_dev), "/dev/%s", netif);

		tap_fd = open(tap_dev, TAPDEV_OFLAGS);

		/*
		 * If we cannot open normally try to connect to it.
		 */
		if (tap_fd < 0)
			tap_fd = unix_connect(tap_dev);

		if (tap_fd < 0) {
			warn("Unable to open %s", tap_dev);
			return -1;
		}
	}

	/*
	 * Check whether the device file is a tap(4)
	 */
	if (fstat(tap_fd, &st) < 0) {
		failed = 1;
	} else if (S_ISCHR(st.st_mode)) {
		dname = fdevname(tap_fd);
		if (dname)
			dname = strstr(dname, "tap");
		if (dname) {
			/*
			 * Bring up the corresponding tap(4) interface
			 */
			*tap_unit = strtol(dname + 3, NULL, 10);
			printf("TAP UNIT %d\n", *tap_unit);
			if (netif_set_tapflags(*tap_unit, IFF_UP, s) == 0)
				failed = 0;
			else
				failed = 1;
		} else {
			failed = 1;
		}
	} else if (S_ISSOCK(st.st_mode)) {
		/*
		 * Special socket connection (typically to vknet).  We
		 * do not have to do anything.
		 */
		failed = 0;
	} else {
		failed = 1;
	}

	if (failed) {
		warnx("%s is not a tap(4) device or socket", tap_dev);
		close(tap_fd);
		tap_fd = -1;
		*tap_unit = -1;
	}
	return tap_fd;
}

static int
unix_connect(const char *path)
{
	struct sockaddr_un sunx;
	int len;
	int net_fd;
	int sndbuf = 262144;
	struct stat st;

	snprintf(sunx.sun_path, sizeof(sunx.sun_path), "%s", path);
	len = offsetof(struct sockaddr_un, sun_path[strlen(sunx.sun_path)]);
	++len;	/* include nul */
	sunx.sun_family = AF_UNIX;
	sunx.sun_len = len;

	net_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (net_fd < 0)
		return(-1);
	if (connect(net_fd, (void *)&sunx, len) < 0) {
		close(net_fd);
		return(-1);
	}
	setsockopt(net_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	if (fstat(net_fd, &st) == 0)
		printf("Network socket buffer: %d bytes\n", st.st_blksize);
	fcntl(net_fd, F_SETFL, O_NONBLOCK);
	return(net_fd);
}

#undef TAPDEV_MAJOR
#undef TAPDEV_MINOR
#undef TAPDEV_OFLAGS

/*
 * Following syntax is supported,
 * 1) x.x.x.x             tap(4)'s address is x.x.x.x
 *
 * 2) x.x.x.x/z           tap(4)'s address is x.x.x.x
 *                        tap(4)'s netmask len is z
 *
 * 3) x.x.x.x:y.y.y.y     tap(4)'s address is x.x.x.x
 *                        pseudo netif's address is y.y.y.y
 *
 * 4) x.x.x.x:y.y.y.y/z   tap(4)'s address is x.x.x.x
 *                        pseudo netif's address is y.y.y.y
 *                        tap(4) and pseudo netif's netmask len are z
 *
 * 5) bridgeX             tap(4) will be added to bridgeX
 *
 * 6) bridgeX:y.y.y.y     tap(4) will be added to bridgeX
 *                        pseudo netif's address is y.y.y.y
 *
 * 7) bridgeX:y.y.y.y/z   tap(4) will be added to bridgeX
 *                        pseudo netif's address is y.y.y.y
 *                        pseudo netif's netmask len is z
 */
static
int
netif_init_tap(int tap_unit, in_addr_t *addr, in_addr_t *mask, int s)
{
	in_addr_t tap_addr, netmask, netif_addr;
	int next_netif_addr;
	char *tok, *masklen_str, *ifbridge;

	*addr = 0;
	*mask = 0;

	tok = strtok(NULL, ":/");
	if (tok == NULL) {
		/*
		 * Nothing special, simply use tap(4) as backend
		 */
		return 0;
	}

	if (inet_pton(AF_INET, tok, &tap_addr) > 0) {
		/*
		 * tap(4)'s address is supplied
		 */
		ifbridge = NULL;

		/*
		 * If there is next token, then it may be pseudo
		 * netif's address or netmask len for tap(4)
		 */
		next_netif_addr = 0;
	} else {
		/*
		 * Not tap(4)'s address, assume it as a bridge(4)
		 * iface name
		 */
		tap_addr = 0;
		ifbridge = tok;

		/*
		 * If there is next token, then it must be pseudo
		 * netif's address
		 */
		next_netif_addr = 1;
	}

	netmask = netif_addr = 0;

	tok = strtok(NULL, ":/");
	if (tok == NULL)
		goto back;

	if (inet_pton(AF_INET, tok, &netif_addr) <= 0) {
		if (next_netif_addr) {
			warnx("Invalid pseudo netif address: %s", tok);
			return -1;
		}
		netif_addr = 0;

		/*
		 * Current token is not address, then it must be netmask len
		 */
		masklen_str = tok;
	} else {
		/*
		 * Current token is pseudo netif address, if there is next token
		 * it must be netmask len
		 */
		masklen_str = strtok(NULL, "/");
	}

	/* Calculate netmask */
	if (masklen_str != NULL) {
		u_long masklen;

		masklen = strtoul(masklen_str, NULL, 10);
		if (masklen < 32 && masklen > 0) {
			netmask = htonl(~((1LL << (32 - masklen)) - 1)
					& 0xffffffff);
		} else {
			warnx("Invalid netmask len: %lu", masklen);
			return -1;
		}
	}

	/* Make sure there is no more token left */
	if (strtok(NULL, ":/") != NULL) {
		warnx("Invalid argument to '-I'");
		return -1;
	}

back:
	if (tap_unit < 0) {
		/* Do nothing */
	} else if (ifbridge == NULL) {
		/* Set tap(4) address/netmask */
		if (netif_set_tapaddr(tap_unit, tap_addr, netmask, s) < 0)
			return -1;
	} else {
		/* Tie tap(4) to bridge(4) */
		if (netif_add_tap2brg(tap_unit, ifbridge, s) < 0)
			return -1;
	}

	*addr = netif_addr;
	*mask = netmask;
	return 0;
}

/*
 * NetifInfo[] will be filled for pseudo netif initialization.
 * NetifNum will be bumped to reflect the number of valid entries
 * in NetifInfo[].
 */
static
void
init_netif(char *netifExp[], int netifExpNum)
{
	int i, s;
	char *tmp;

	if (netifExpNum == 0)
		return;

	s = socket(AF_INET, SOCK_DGRAM, 0);	/* for ioctl(SIOC) */
	if (s < 0)
		return;

	for (i = 0; i < netifExpNum; ++i) {
		struct vknetif_info *info;
		in_addr_t netif_addr, netif_mask;
		int tap_fd, tap_unit;
		char *netif;

		/* Extract MAC address if there is one */
		tmp = netifExp[i];
		strsep(&tmp, "=");

		netif = strtok(netifExp[i], ":");
		if (netif == NULL) {
			warnx("Invalid argument to '-I'");
			continue;
		}

		/*
		 * Open tap(4) device file and bring up the
		 * corresponding interface
		 */
		tap_fd = netif_open_tap(netif, &tap_unit, s);
		if (tap_fd < 0)
			continue;

		/*
		 * Initialize tap(4) and get address/netmask
		 * for pseudo netif
		 *
		 * NB: Rest part of netifExp[i] is passed
		 *     to netif_init_tap() implicitly.
		 */
		if (netif_init_tap(tap_unit, &netif_addr, &netif_mask, s) < 0) {
			/*
			 * NB: Closing tap(4) device file will bring
			 *     down the corresponding interface
			 */
			close(tap_fd);
			continue;
		}

		info = &NetifInfo[NetifNum];
		bzero(info, sizeof(*info));
		info->tap_fd = tap_fd;
		info->tap_unit = tap_unit;
		info->netif_addr = netif_addr;
		info->netif_mask = netif_mask;
		/*
		 * If tmp isn't NULL it means a MAC could have been
		 * specified so attempt to convert it.
		 * Setting enaddr to NULL will tell vke_attach() we
		 * need a pseudo-random MAC address.
		 */
		if (tmp != NULL) {
			if ((info->enaddr = malloc(ETHER_ADDR_LEN)) == NULL)
				warnx("Couldn't allocate memory for the operation");
			else {
				if ((kether_aton(tmp, info->enaddr)) == NULL) {
					free(info->enaddr);
					info->enaddr = NULL;
				}
			}
		}

		NetifNum++;
		if (NetifNum >= VKNETIF_MAX)	/* XXX will this happen? */
			break;
	}
	close(s);
}

/*
 * Create the pid file and leave it open and locked while the vkernel is
 * running.  This allows a script to use /usr/bin/lockf to probe whether
 * a vkernel is still running (so as not to accidently kill an unrelated
 * process from a stale pid file).
 */
static
void
writepid(void)
{
	char buf[32];
	int fd;

	if (pid_file != NULL) {
		snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
		fd = open(pid_file, O_RDWR|O_CREAT|O_EXLOCK|O_NONBLOCK, 0666);
		if (fd < 0) {
			if (errno == EWOULDBLOCK) {
				perror("Failed to lock pidfile, "
				       "vkernel already running");
			} else {
				perror("Failed to create pidfile");
			}
			exit(EX_SOFTWARE);
		}
		ftruncate(fd, 0);
		write(fd, buf, strlen(buf));
		/* leave the file open to maintain the lock */
	}
}

static
void
cleanpid( void )
{
	if (pid_file != NULL) {
		if (unlink(pid_file) < 0)
			perror("Warning: couldn't remove pidfile");
	}
}

static
void
usage_err(const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vfprintf(stderr, ctl, va);
	va_end(va);
	fprintf(stderr, "\n");
	exit(EX_USAGE);
}

static
void
usage_help(_Bool help)
{
	fprintf(stderr, "Usage: %s [-hsUvd] [-c file] [-e name=value:name=value:...]\n"
	    "\t[-i file] [-I interface[:address1[:address2][/netmask]]] [-l cpulock]\n"
	    "\t[-m size] [-n numcpus[:lbits[:cbits]]]\n"
	    "\t[-p file] [-r file]\n", save_av[0]);

	if (help)
		fprintf(stderr, "\nArguments:\n"
		    "\t-c\tSpecify a readonly CD-ROM image file to be used by the kernel.\n"
		    "\t-e\tSpecify an environment to be used by the kernel.\n"
		    "\t-h\tThis list of options.\n"
		    "\t-i\tSpecify a memory image file to be used by the virtual kernel.\n"
		    "\t-I\tCreate a virtual network device.\n"
		    "\t-l\tSpecify which, if any, real CPUs to lock virtual CPUs to.\n"
		    "\t-m\tSpecify the amount of memory to be used by the kernel in bytes.\n"
		    "\t-n\tSpecify the number of CPUs and the topology you wish to emulate:\n"
		    "\t  \t- numcpus - number of cpus\n"
		    "\t  \t- :lbits - specify the number of bits within APICID(=CPUID) needed for representing\n"
		    "\t  \t  the logical ID. Controls the number of threads/core (0bits - 1 thread, 1bit - 2 threads).\n"
		    "\t  \t- :cbits - specify the number of bits within APICID(=CPUID) needed for representing\n"
		    "\t  \t  the core ID. Controls the number of core/package (0bits - 1 core, 1bit - 2 cores).\n"
		    "\t-p\tSpecify a file in which to store the process ID.\n"
		    "\t-r\tSpecify a R/W disk image file to be used by the kernel.\n"
		    "\t-s\tBoot into single-user mode.\n"
		    "\t-U\tEnable writing to kernel memory and module loading.\n"
		    "\t-v\tTurn on verbose booting.\n");

	exit(EX_USAGE);
}

void
cpu_reset(void)
{
	kprintf("cpu reset, rebooting vkernel\n");
	closefrom(3);
	cleanpid();
	exit(EX_VKERNEL_REBOOT);

}

void
cpu_halt(void)
{
	kprintf("cpu halt, exiting vkernel\n");
	cleanpid();
	exit(EX_OK);
}

void
setrealcpu(void)
{
	switch(lwp_cpu_lock) {
	case LCL_PER_CPU:
		if (bootverbose)
			kprintf("Locking CPU%d to real cpu %d\n",
				mycpuid, next_cpu);
		usched_set(getpid(), USCHED_SET_CPU, &next_cpu, sizeof(next_cpu));
		next_cpu++;
		if (next_cpu >= real_ncpus)
			next_cpu = 0;
		break;
	case LCL_SINGLE_CPU:
		if (bootverbose)
			kprintf("Locking CPU%d to real cpu %d\n",
				mycpuid, next_cpu);
		usched_set(getpid(), USCHED_SET_CPU, &next_cpu, sizeof(next_cpu));
		break;
	default:
		/* do not map virtual cpus to real cpus */
		break;
	}
}

/*
 * Allocate and free memory for module loading.  The loaded module
 * has to be placed somewhere near the current kernel binary load
 * point or the relocations will not work.
 *
 * I'm not sure why this isn't working.
 */
int
vkernel_module_memory_alloc(vm_offset_t *basep, size_t bytes)
{
#if 1
	size_t xtra;
	xtra = (PAGE_SIZE - (vm_offset_t)sbrk(0)) & PAGE_MASK;
	*basep = (vm_offset_t)sbrk(xtra + bytes) + xtra;
	bzero((void *)*basep, bytes);
#else
	*basep = (vm_offset_t)mmap((void *)0x000000000, bytes,
				   PROT_READ|PROT_WRITE|PROT_EXEC,
				   MAP_ANON|MAP_SHARED, -1, 0);
	if ((void *)*basep == MAP_FAILED)
		return ENOMEM;
#endif
	return 0;
}

void
vkernel_module_memory_free(vm_offset_t base, size_t bytes)
{
#if 0
#if 0
	munmap((void *)base, bytes);
#endif
#endif
}
