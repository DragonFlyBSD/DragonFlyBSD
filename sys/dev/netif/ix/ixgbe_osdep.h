/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXGBE_OSDEP_H_
#define _IXGBE_OSDEP_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/clock.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#define ASSERT(x) if(!(x)) panic("IXGBE: x")
#define EWARN(H, W) kprintf(W)

/* The happy-fun DELAY macro is defined in /usr/src/sys/i386/include/clock.h */
#define usec_delay(x) DELAY(x)
#define msec_delay(x) DELAY(1000*(x))

#define DBG 0
#define MSGOUT(S, A, B)     kprintf(S "\n", A, B)
#define DEBUGFUNC(F)        DEBUGOUT(F);
#if DBG
	#define DEBUGOUT(S)         kprintf(S "\n")
	#define DEBUGOUT1(S,A)      kprintf(S "\n",A)
	#define DEBUGOUT2(S,A,B)    kprintf(S "\n",A,B)
	#define DEBUGOUT3(S,A,B,C)  kprintf(S "\n",A,B,C)
	#define DEBUGOUT4(S,A,B,C,D)  kprintf(S "\n",A,B,C,D)
	#define DEBUGOUT5(S,A,B,C,D,E)  kprintf(S "\n",A,B,C,D,E)
	#define DEBUGOUT6(S,A,B,C,D,E,F)  kprintf(S "\n",A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)  kprintf(S "\n",A,B,C,D,E,F,G)
	#define ERROR_REPORT1(S,A)      kprintf(S "\n",A)
	#define ERROR_REPORT2(S,A,B)    kprintf(S "\n",A,B)
	#define ERROR_REPORT3(S,A,B,C)  kprintf(S "\n",A,B,C)
#else
	#define DEBUGOUT(S)		do { } while (0)
	#define DEBUGOUT1(S,A)		do { } while (0)
	#define DEBUGOUT2(S,A,B)	do { } while (0)
	#define DEBUGOUT3(S,A,B,C)	do { } while (0)
	#define DEBUGOUT4(S,A,B,C,D)	do { } while (0)
	#define DEBUGOUT5(S,A,B,C,D,E)	do { } while (0)
	#define DEBUGOUT6(S,A,B,C,D,E,F) do { } while (0)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G) do { } while (0)

	#define ERROR_REPORT1(S,A)	do { } while (0)
	#define ERROR_REPORT2(S,A,B)	do { } while (0)
	#define ERROR_REPORT3(S,A,B,C)	do { } while (0)
#endif

#define FALSE               0
#define false               0 /* shared code requires this */
#define TRUE                1
#define true                1
#define CMD_MEM_WRT_INVALIDATE          0x0010  /* BIT_4 */
#define PCI_COMMAND_REGISTER            PCIR_COMMAND

/* Shared code dropped this define.. */
#define IXGBE_INTEL_VENDOR_ID		0x8086

/* Bunch of defines for shared code bogosity */
#define UNREFERENCED_PARAMETER(_p)
#define UNREFERENCED_1PARAMETER(_p)
#define UNREFERENCED_2PARAMETER(_p, _q)
#define UNREFERENCED_3PARAMETER(_p, _q, _r)
#define UNREFERENCED_4PARAMETER(_p, _q, _r, _s)

#define IXGBE_NTOHL(_i)	ntohl(_i)
#define IXGBE_NTOHS(_i)	ntohs(_i)

/* XXX these need to be revisited */
#define IXGBE_CPU_TO_LE16 htole16
#define IXGBE_CPU_TO_LE32 htole32
#define IXGBE_LE32_TO_CPU le32toh
#define IXGBE_LE32_TO_CPUS(x)
#define IXGBE_CPU_TO_BE16 htobe16
#define IXGBE_CPU_TO_BE32 htobe32
#define IXGBE_BE32_TO_CPU be32toh

typedef uint8_t		u8;
typedef int8_t		s8;
typedef uint16_t	u16;
typedef int16_t		s16;
typedef uint32_t	u32;
typedef int32_t		s32;
typedef uint64_t	u64;

/* shared code requires this */
#define __le16  u16
#define __le32  u32
#define __le64  u64
#define __be16  u16
#define __be32  u32
#define __be64  u64

#define le16_to_cpu

#if defined(__i386__) || defined(__x86_64__)
#define mb()	__asm volatile("mfence" ::: "memory")
#define wmb()	__asm volatile("sfence" ::: "memory")
#define rmb()	__asm volatile("lfence" ::: "memory")
#else
#define mb()
#define rmb()
#define wmb()
#endif

#if defined(__i386__) || defined(__x86_64__)
static __inline
void prefetch(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
#else
#define prefetch(x)
#endif

/*
 * Optimized bcopy thanks to Luigi Rizzo's investigative work.  Assumes
 * non-overlapping regions and 32-byte padding on both src and dst.
 */
static __inline int
ixgbe_bcopy(void *restrict _src, void *restrict _dst, int l)
{
	uint64_t *src = _src;
	uint64_t *dst = _dst;

	for (; l > 0; l -= 32) {
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
	}
	return (0);
}

struct ixgbe_osdep
{
	bus_space_tag_t    mem_bus_space_tag;
	bus_space_handle_t mem_bus_space_handle;
};

/* These routines need struct ixgbe_hw declared */
struct ixgbe_hw;

/* These routines are needed by the shared code */
extern u16 ixgbe_read_pci_cfg_vf(struct ixgbe_hw *, u32);
extern u16 ixgbe_read_pci_cfg_pf(struct ixgbe_hw *, u32);

extern void ixgbe_write_pci_cfg_vf(struct ixgbe_hw *, u32, u16);
extern void ixgbe_write_pci_cfg_pf(struct ixgbe_hw *, u32, u16);

#define IXGBE_WRITE_FLUSH(a) IXGBE_READ_REG(a, IXGBE_STATUS)

extern u32 ixgbe_read_reg_vf(struct ixgbe_hw *, u32);
extern u32 ixgbe_read_reg_pf(struct ixgbe_hw *, u32);
extern void ixgbe_write_reg_vf(struct ixgbe_hw *, u32, u32);
extern void ixgbe_write_reg_pf(struct ixgbe_hw *, u32, u32);

extern u32 ixgbe_read_reg_array_vf(struct ixgbe_hw *, u32, u32);
extern u32 ixgbe_read_reg_array_pf(struct ixgbe_hw *, u32, u32);

extern void ixgbe_write_reg_array_vf(struct ixgbe_hw *, u32, u32, u32);
extern void ixgbe_write_reg_array_pf(struct ixgbe_hw *, u32, u32, u32);

#define IXGBE_READ_REG ixgbe_read_reg_pf
#define IXGBE_READ_PCIE_WORD ixgbe_read_pci_cfg_pf
#define IXGBE_WRITE_PCIE_WORD ixgbe_write_pci_cfg_pf
#define IXGBE_WRITE_REG ixgbe_write_reg_pf
#define IXGBE_READ_REG_ARRAY ixgbe_read_reg_array_pf
#define IXGBE_WRITE_REG_ARRAY ixgbe_write_reg_array_pf

#endif /* _IXGBE_OSDEP_H_ */
