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
 * $DragonFly: src/sys/platform/pc64/amd64/init.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/init.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/cons.h>
#include <sys/random.h>
#include <sys/tls.h>
#include <sys/reboot.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/vmspace.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <vm/vm_page.h>

#include <machine/cpu.h>
#include <machine/globaldata.h>
#include <machine/tls.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/bridge/if_bridgevar.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct privatespace CPU_prvspace[];

vm_paddr_t phys_avail[16];
vm_paddr_t Maxmem;
vm_paddr_t Maxmem_bytes;
int MemImageFd = -1;
int DiskNum;
int NetifNum;
char *pid_file;
struct msgbuf *msgbufp;
caddr_t ptvmmap;
u_int tsc_present; 
vm_offset_t KvaStart;
vm_offset_t KvaEnd;
vm_offset_t KvaSize;
vm_offset_t virtual_start;
vm_offset_t virtual_end;
vm_offset_t kernel_vm_end;
vm_offset_t crashdumpmap;
vm_offset_t clean_sva;
vm_offset_t clean_eva;

static void init_sys_memory(char *imageFile);
static void init_kern_memory(void);
static void init_kernel(void);
static void init_globaldata(void);
static void init_netif(char *netifExp[], int netifFileNum);
static void writepid( void );
static void cleanpid( void );
static void usage(const char *ctl, ...);

/*
 * Kernel startup for virtual kernels - standard main() 
 */
int
main(int ac, char **av)
{	
	/* NOT REACHED */
}

/*
 * Initialize system memory.  This is the virtual kernel's 'RAM'.
 */
static
void
init_sys_memory(char *imageFile)
{
}

/*
 * Initialize kernel memory.  This reserves kernel virtual memory by using
 * MAP_VPAGETABLE
 */

static
void
init_kern_memory(void)
{
}

/*
 * Map the per-cpu globaldata for cpu #0.  Allocate the space using
 * virtual_start and phys_avail[0]
 */
static
void
init_globaldata(void)
{
}

/*
 * Initialize very low level systems including thread0, proc0, etc.
 */
static
void
init_kernel(void)
{
}

void
init_netif(char *netifExp[], int netifExpNum)
{
}

static
void
writepid( void )
{
}

static
void
cleanpid( void ) 
{
}

static
void
usage(const char *ctl, ...)
{
}

void
cpu_reset(void)
{
}

void
cpu_halt(void)
{
}
