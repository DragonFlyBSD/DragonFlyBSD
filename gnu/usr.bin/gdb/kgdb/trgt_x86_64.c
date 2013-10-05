/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/trgt_amd64.c,v 1.10 2008/05/01 20:36:48 jhb Exp $
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/thread.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <err.h>
#include <kvm.h>
#include <string.h>

#include <defs.h>
#include <target.h>
#include <gdbthread.h>
#include <inferior.h>
#include <regcache.h>
#include <frame-unwind.h>
#include <amd64-tdep.h>

#include "kgdb.h"

static int
kgdb_trgt_trapframe_sniffer(const struct frame_unwind *self,
			    struct frame_info *next_frame,
			    void **this_prologue_cache);

void
kgdb_trgt_fetch_registers(struct target_ops *target_ops, struct regcache *regcache, int regno)
{
	struct kthr *kt;
	struct pcb pcb;

	kt = kgdb_thr_lookup_tid(ptid_get_tid(inferior_ptid));
	if (kt == NULL) {
		regcache_raw_supply(regcache, regno, NULL);
		return;
	}

	/*
	 * kt->pcb == 0 is a marker for "non-dumping kernel thread".
	 */
	if (kt->pcb == 0) {
		uintptr_t regs[7];
		uintptr_t addr;
		uintptr_t sp;

		addr = kt->kaddr + offsetof(struct thread, td_sp);
		kvm_read(kvm, addr, &sp, sizeof(sp));
		/*
		 * Stack is:
		 * -2 ret
		 * -1 popfq
		 * 0 popq %r15	edi
		 * 1 popq %r14
		 * 2 popq %r13
		 * 3 popq %r12
		 * 4 popq %rbx
		 * 5 popq %rbp
		 * 6 ret
		 */
		if (kvm_read(kvm, sp + 2 * sizeof(regs[0]), regs, sizeof(regs)) != sizeof(regs)) {
			warnx("kvm_read: %s", kvm_geterr(kvm));
			memset(regs, 0, sizeof(regs));
		}
		regcache_raw_supply(regcache, AMD64_R8_REGNUM + 7, &regs[0]);
		regcache_raw_supply(regcache, AMD64_R8_REGNUM + 6, &regs[1]);
		regcache_raw_supply(regcache, AMD64_R8_REGNUM + 5, &regs[2]);
		regcache_raw_supply(regcache, AMD64_R8_REGNUM + 4, &regs[3]);
		regcache_raw_supply(regcache, AMD64_RBX_REGNUM, &regs[4]);
		regcache_raw_supply(regcache, AMD64_RBP_REGNUM, &regs[5]);
		regcache_raw_supply(regcache, AMD64_RIP_REGNUM, &regs[6]);
		sp += 9 * sizeof(regs[0]);
		regcache_raw_supply(regcache, AMD64_RSP_REGNUM, &sp);
		return;
	}

	if (kvm_read(kvm, kt->pcb, &pcb, sizeof(pcb)) != sizeof(pcb)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		memset(&pcb, 0, sizeof(pcb));
	}

	regcache_raw_supply(regcache, AMD64_RBX_REGNUM, (char *)&pcb.pcb_rbx);
	regcache_raw_supply(regcache, AMD64_RBP_REGNUM, (char *)&pcb.pcb_rbp);
	regcache_raw_supply(regcache, AMD64_RSP_REGNUM, (char *)&pcb.pcb_rsp);
	regcache_raw_supply(regcache, AMD64_R8_REGNUM + 4, (char *)&pcb.pcb_r12);
	regcache_raw_supply(regcache, AMD64_R8_REGNUM + 5, (char *)&pcb.pcb_r13);
	regcache_raw_supply(regcache, AMD64_R8_REGNUM + 6, (char *)&pcb.pcb_r14);
	regcache_raw_supply(regcache, AMD64_R15_REGNUM, (char *)&pcb.pcb_r15);
	regcache_raw_supply(regcache, AMD64_RIP_REGNUM, (char *)&pcb.pcb_rip);
}

struct kgdb_frame_cache {
	int		frame_type;
	CORE_ADDR	pc;
	CORE_ADDR	sp;
};

#define FT_NORMAL		1
#define FT_INTRFRAME		2
/*#define	FT_INTRTRAPFRAME        3*/
#define FT_TIMERFRAME		4
#define FT_CALLTRAP		5

static int kgdb_trgt_frame_offset[20] = {
	offsetof(struct trapframe, tf_rax),
	offsetof(struct trapframe, tf_rbx),
	offsetof(struct trapframe, tf_rcx),
	offsetof(struct trapframe, tf_rdx),
	offsetof(struct trapframe, tf_rsi),
	offsetof(struct trapframe, tf_rdi),
	offsetof(struct trapframe, tf_rbp),
	offsetof(struct trapframe, tf_rsp),
	offsetof(struct trapframe, tf_r8),
	offsetof(struct trapframe, tf_r9),
	offsetof(struct trapframe, tf_r10),
	offsetof(struct trapframe, tf_r11),
	offsetof(struct trapframe, tf_r12),
	offsetof(struct trapframe, tf_r13),
	offsetof(struct trapframe, tf_r14),
	offsetof(struct trapframe, tf_r15),
	offsetof(struct trapframe, tf_rip),
	offsetof(struct trapframe, tf_rflags),
	offsetof(struct trapframe, tf_cs),
	offsetof(struct trapframe, tf_ss)
};

static struct kgdb_frame_cache *
kgdb_trgt_frame_cache(struct frame_info *next_frame, void **this_cache)
{
	struct kgdb_frame_cache *cache;
	const char *pname;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_frame_cache);
		*this_cache = cache;
		cache->pc = get_frame_address_in_block(next_frame);
		cache->sp = get_frame_sp(next_frame);
		find_pc_partial_function(cache->pc, &pname, NULL, NULL);

		if (strcmp(pname, "calltrap") == 0)
			cache->frame_type = FT_CALLTRAP;
		else if (pname[0] != 'X')
			cache->frame_type = FT_NORMAL;
		else if (strcmp(pname, "Xtimerint") == 0)
			cache->frame_type = FT_TIMERFRAME;
		else
			cache->frame_type = FT_INTRFRAME;
	}
	return (cache);
}

static void
kgdb_trgt_trapframe_this_id(struct frame_info *next_frame, void **this_cache,
    struct frame_id *this_id)
{
	struct kgdb_frame_cache *cache;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);
	*this_id = frame_id_build(cache->sp, cache->pc);
}

static struct value *
kgdb_trgt_trapframe_prev_register(struct frame_info *next_frame,
    void **this_cache, int regnum)
{
	CORE_ADDR addrp;
	struct kgdb_frame_cache *cache;
	int ofs;

	if (regnum < AMD64_RAX_REGNUM || regnum > AMD64_EFLAGS_REGNUM + 2)
		return frame_unwind_got_register(next_frame, regnum, regnum);

	ofs = kgdb_trgt_frame_offset[regnum];

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);

	switch (cache->frame_type) {
	case FT_NORMAL:
		break;
	case FT_INTRFRAME:
		ofs += 8;
		break;
	case FT_TIMERFRAME:
		break;
		/*
	case FT_INTRTRAPFRAME:
		ofs -= ofs_fix;
		break;
		*/
	case FT_CALLTRAP:
		ofs += 0;
		break;
	default:
		fprintf_unfiltered(gdb_stderr, "Correct FT_XXX frame offsets "
		   "for %d\n", cache->frame_type);
		break;
	}

	addrp = cache->sp + ofs;
	return frame_unwind_got_memory(next_frame, regnum, addrp);
}

static enum unwind_stop_reason
kgdb_trgt_trapframe_unwind_reason(struct frame_info *next_frame,
    void **this_cache)
{
    /* XXX marino : populate logic to determine unwind stoppage */
    return UNWIND_NO_REASON;
}

const struct frame_unwind kgdb_trgt_trapframe_unwind = {
        NORMAL_FRAME,
        &kgdb_trgt_trapframe_unwind_reason,
        &kgdb_trgt_trapframe_this_id,
        &kgdb_trgt_trapframe_prev_register,
	.sniffer = kgdb_trgt_trapframe_sniffer
};

static int
kgdb_trgt_trapframe_sniffer(const struct frame_unwind *self,
			    struct frame_info *next_frame,
			    void **this_prologue_cache)
{
	const char *pname;
	CORE_ADDR pc;

	pc = get_frame_address_in_block(next_frame);
	pname = NULL;
	find_pc_partial_function(pc, &pname, NULL, NULL);
	if (pname == NULL)
		return (0);
	if (strcmp(pname, "calltrap") == 0 ||
	    strcmp(pname, "dblfault_handler") == 0 ||
	    strcmp(pname, "nmi_calltrap") == 0 ||
	    (pname[0] == 'X' && pname[1] != '_'))
		return (1);
	return (0);
}
