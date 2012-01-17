/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/machintr.h,v 1.7 2007/04/30 16:46:01 dillon Exp $
 */
/*
 * This module defines the ABI for the machine-independant cpu interrupt
 * vector and masking layer.
 */

#ifndef _SYS_MACHINTR_H_
#define _SYS_MACHINTR_H_

#ifdef _KERNEL

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

struct rman;

enum machintr_type { MACHINTR_GENERIC, MACHINTR_ICU, MACHINTR_IOAPIC };

#define MACHINTR_VECTOR_SETUP		1
#define MACHINTR_VECTOR_TEARDOWN	2

/*
 * Machine interrupt ABIs - registered at boot-time
 */
struct machintr_abi {
    enum machintr_type type;

    void	(*intr_disable)(int intr);	/* hardware disable intr */
    void	(*intr_enable)(int intr);	/* hardware enable intr */
    void	(*intr_setup)(int intr, int flags);
    						/* setup intr */
    void	(*intr_teardown)(int intr);	/* tear down intr */

    void	(*legacy_intr_config)		/* config legacy intr */
    		(int intr, enum intr_trigger trig, enum intr_polarity pola);
    int		(*legacy_intr_cpuid)(int intr);	/* legacy intr target cpu */

    int		(*msi_alloc)			/* alloc count MSIs on cpu */
    		(int intrs[], int count, int cpuid);
    void	(*msi_release)			/* release count MSIs on cpu */
    		(const int intrs[], int count, int cpuid);
    void	(*msi_map)			/* addr/data for MSI on cpu */
    		(int intr, uint64_t *addr, uint32_t *data, int cpuid);
    int		(*msix_alloc)			/* alloc one MSI-X on cpu */
		(int *intr, int cpuid);
    void	(*msix_release)			/* release one MSI-X on cpu */
		(int intr, int cpuid);

    void	(*finalize)(void);		/* final before ints enabled */
    void	(*cleanup)(void);		/* cleanup */
    void	(*setdefault)(void);		/* set default vectors */
    void	(*stabilize)(void);		/* stable before ints enabled */
    void	(*initmap)(void);		/* init irq mapping */
    void	(*rman_setup)(struct rman *rm);	/* setup rman */
};

#define machintr_intr_enable(intr)	MachIntrABI.intr_enable(intr)
#define machintr_intr_disable(intr)	MachIntrABI.intr_disable(intr)
#define machintr_intr_setup(intr, flags) \
	    MachIntrABI.intr_setup((intr), (flags))
#define machintr_intr_teardown(intr) \
	    MachIntrABI.intr_teardown((intr))

#define machintr_legacy_intr_config(intr, trig, pola) \
	    MachIntrABI.legacy_intr_config((intr), (trig), (pola))
#define machintr_legacy_intr_cpuid(intr) \
	    MachIntrABI.legacy_intr_cpuid((intr))

extern struct machintr_abi MachIntrABI;

#endif	/* _KERNEL */

#endif	/* _SYS_MACHINTR_H_ */
