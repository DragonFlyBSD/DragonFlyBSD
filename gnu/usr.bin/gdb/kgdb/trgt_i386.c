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
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/trgt_i386.c,v 1.5 2005/09/11 05:36:30 marcel Exp $
 * $DragonFly: src/gnu/usr.bin/gdb/kgdb/trgt_i386.c,v 1.5 2008/01/14 21:36:38 corecode Exp $
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <machine/thread.h>
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
#include <i386-tdep.h>

#include "kgdb.h"

void
kgdb_trgt_fetch_registers(struct regcache *regcache, int regno)
{
	struct kthr *kt;
	struct pcb pcb;

	kt = kgdb_thr_lookup_tid(ptid_get_tid(inferior_ptid));
	if (kt == NULL) {
		regcache_raw_supply(regcache, regno, NULL);
		return;
	}

	/*
	 * kt->pcb == NULL is a marker for "non-dumping kernel thread".
	 */
	if (kt->pcb == NULL) {
		uintptr_t regs[5];
		uintptr_t addr;
		uintptr_t sp;

		addr = kt->kaddr + offsetof(struct thread, td_sp);
		kvm_read(kvm, addr, &sp, sizeof(sp));
		/*
		 * Stack is:
		 * -2 ret
		 * -1 popfl
		 * 0 popl %edi
		 * 1 popl %esi
		 * 2 popl %ebx
		 * 3 popl %ebp
		 * 4 ret
		 */
		if (kvm_read(kvm, sp + 2 * sizeof(regs[0]), regs, sizeof(regs)) != sizeof(regs)) {
			warnx("kvm_read: %s", kvm_geterr(kvm));
			memset(regs, 0, sizeof(regs));
		}
		regcache_raw_supply(regcache, I386_EDI_REGNUM, &regs[0]);
		regcache_raw_supply(regcache, I386_ESI_REGNUM, &regs[1]);
		regcache_raw_supply(regcache, I386_EBX_REGNUM, &regs[2]);
		regcache_raw_supply(regcache, I386_EBP_REGNUM, &regs[3]);
		regcache_raw_supply(regcache, I386_EIP_REGNUM, &regs[4]);
		sp += 7 * sizeof(regs[0]);
		regcache_raw_supply(regcache, I386_ESP_REGNUM, &sp);
		return;
	}

	if (kvm_read(kvm, kt->pcb, &pcb, sizeof(pcb)) != sizeof(pcb)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		memset(&pcb, 0, sizeof(pcb));
	}
	regcache_raw_supply(regcache, I386_EBX_REGNUM, (char *)&pcb.pcb_ebx);
	regcache_raw_supply(regcache, I386_ESP_REGNUM, (char *)&pcb.pcb_esp);
	regcache_raw_supply(regcache, I386_EBP_REGNUM, (char *)&pcb.pcb_ebp);
	regcache_raw_supply(regcache, I386_ESI_REGNUM, (char *)&pcb.pcb_esi);
	regcache_raw_supply(regcache, I386_EDI_REGNUM, (char *)&pcb.pcb_edi);
	regcache_raw_supply(regcache, I386_EIP_REGNUM, (char *)&pcb.pcb_eip);
}

void
kgdb_trgt_store_registers(struct regcache *regcache, int regno __unused)
{
	fprintf_unfiltered(gdb_stderr, "XXX: %s\n", __func__);
}

struct kgdb_frame_cache {
	int		intrframe;
	CORE_ADDR	pc;
	CORE_ADDR	sp;
};

static int kgdb_trgt_frame_offset[15] = {
	offsetof(struct trapframe, tf_eax),
	offsetof(struct trapframe, tf_ecx),
	offsetof(struct trapframe, tf_edx),
	offsetof(struct trapframe, tf_ebx),
	offsetof(struct trapframe, tf_esp),
	offsetof(struct trapframe, tf_ebp),
	offsetof(struct trapframe, tf_esi),
	offsetof(struct trapframe, tf_edi),
	offsetof(struct trapframe, tf_eip),
	offsetof(struct trapframe, tf_eflags),
	offsetof(struct trapframe, tf_cs),
	offsetof(struct trapframe, tf_ss),
	offsetof(struct trapframe, tf_ds),
	offsetof(struct trapframe, tf_es),
	offsetof(struct trapframe, tf_fs)
};

static struct kgdb_frame_cache *
kgdb_trgt_frame_cache(struct frame_info *next_frame, void **this_cache)
{
	char buf[MAX_REGISTER_SIZE];
	struct kgdb_frame_cache *cache;
	char *pname;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_frame_cache);
		*this_cache = cache;
		cache->pc = get_frame_address_in_block(next_frame);
		find_pc_partial_function(cache->pc, &pname, NULL, NULL);

		/*
		 * Handle weird trapframe cases:
		 *
		 * 4 for pushed esp (IN the function!)
		 * + 8 for cpl/intno (IN the function, only intrs)
		 */
		cache->intrframe = 4;
		if (pname[0] == 'X')
			cache->intrframe += offsetof(struct intrframe, if_esp) -
					    offsetof(struct trapframe, tf_esp);

		frame_unwind_register(next_frame, I386_ESP_REGNUM, buf);
		cache->sp = extract_unsigned_integer(buf,
		    register_size(current_gdbarch, I386_ESP_REGNUM));
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

static void
kgdb_trgt_trapframe_prev_register(struct frame_info *next_frame,
    void **this_cache, int regnum, int *optimizedp, enum lval_type *lvalp,
    CORE_ADDR *addrp, int *realnump, gdb_byte *valuep)
{
	char dummy_valuep[MAX_REGISTER_SIZE];
	struct kgdb_frame_cache *cache;
	int ofs, regsz;

	regsz = register_size(current_gdbarch, regnum);

	if (valuep == NULL)
		valuep = dummy_valuep;
	memset(valuep, 0, regsz);
	*optimizedp = 0;
	*addrp = 0;
	*lvalp = not_lval;
	*realnump = -1;

	if (regnum < I386_EAX_REGNUM || regnum > I386_FS_REGNUM)
		return;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);

	ofs = kgdb_trgt_frame_offset[regnum];
	*addrp = cache->sp + ofs + cache->intrframe;

	/*
	 * If we are in the kernel, we don't have esp stored in the
	 * trapframe, but we can calculate it simply by subtracting
	 * the size of the frame.
	 */
	if (regnum == I386_ESP_REGNUM) {
		char buf[4];

		frame_unwind_register(next_frame, I386_CS_REGNUM, buf);
		if (extract_unsigned_integer(buf, 4) != SEL_UPL) {
			store_unsigned_integer(valuep, regsz, *addrp);
			return;
		}
		/* FALLTHROUGH */
	}
	*lvalp = lval_memory;
	target_read_memory(*addrp, valuep, regsz);
}

static int
kgdb_trgt_trapframe_sniffer(const struct frame_unwind *self,
			    struct frame_info *next_frame,
			    void **this_prologue_cache)
{
	char *pname;
	CORE_ADDR pc;

	pc = frame_unwind_address_in_block(next_frame, NORMAL_FRAME);
	pname = NULL;
	find_pc_partial_function(pc, &pname, NULL, NULL);
	if (pname == NULL)
		return (NULL);
	if (strcmp(pname, "calltrap") == 0 ||
	    strcmp(pname, "dblfault_handler") == 0 ||
	    (pname[0] == 'X' && pname[1] != '_'))
		return 1;
	/* printf("%s: %llx =%s\n", __func__, pc, pname); */
	return 0;
}

const struct frame_unwind kgdb_trgt_trapframe_unwind = {
        NORMAL_FRAME,
        &kgdb_trgt_trapframe_this_id,
        &kgdb_trgt_trapframe_prev_register,
	.sniffer = kgdb_trgt_trapframe_sniffer
};
