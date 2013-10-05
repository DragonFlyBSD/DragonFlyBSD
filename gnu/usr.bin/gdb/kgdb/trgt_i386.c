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
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/trgt_i386.c,v 1.13 2008/09/27 15:58:37 kib Exp $
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <machine/thread.h>
#include <sys/thread.h>
#include <machine/globaldata.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/segments.h>
#include <machine/tss.h>
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

struct kgdb_tss_cache {
	CORE_ADDR	pc;
	CORE_ADDR	sp;
	CORE_ADDR	tss;
};

static int kgdb_trgt_tss_offset[15] = {
	offsetof(struct i386tss, tss_eax),
	offsetof(struct i386tss, tss_ecx),
	offsetof(struct i386tss, tss_edx),
	offsetof(struct i386tss, tss_ebx),
	offsetof(struct i386tss, tss_esp),
	offsetof(struct i386tss, tss_ebp),
	offsetof(struct i386tss, tss_esi),
	offsetof(struct i386tss, tss_edi),
	offsetof(struct i386tss, tss_eip),
	offsetof(struct i386tss, tss_eflags),
	offsetof(struct i386tss, tss_cs),
	offsetof(struct i386tss, tss_ss),
	offsetof(struct i386tss, tss_ds),
	offsetof(struct i386tss, tss_es),
	offsetof(struct i386tss, tss_fs)
};

/*
 * If the current thread is executing on a CPU, fetch the common_tss
 * for that CPU.
 *
 * This is painful because 'struct pcpu' is variant sized, so we can't
 * use it.  Instead, we lookup the GDT selector for this CPU and
 * extract the base of the TSS from there.
 */
static CORE_ADDR
kgdb_trgt_fetch_tss(void)
{
	struct kthr *kt;
	struct segment_descriptor sd;
	uintptr_t addr, tss;

	kt = kgdb_thr_lookup_tid(ptid_get_tid(inferior_ptid));
	if (kt == NULL || kt->gd == 0)
		return (0);

	addr = kt->gd + offsetof(struct mdglobaldata, gd_common_tssd);
	if (kvm_read(kvm, addr, &sd, sizeof(sd)) != sizeof(sd)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		return (0);
	}
	if (sd.sd_type != SDT_SYS386BSY) {
		warnx("descriptor is not a busy TSS");
		return (0);
	}
	tss = kt->gd + offsetof(struct mdglobaldata, gd_common_tss);

	return ((CORE_ADDR)tss);
}

static struct kgdb_tss_cache *
kgdb_trgt_tss_cache(struct frame_info *next_frame, void **this_cache)
{
	struct gdbarch *gdbarch = get_frame_arch(next_frame);
	enum bfd_endian byte_order = gdbarch_byte_order(gdbarch);
	char buf[MAX_REGISTER_SIZE];
	struct kgdb_tss_cache *cache;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_tss_cache);
		*this_cache = cache;
		cache->pc = get_frame_address_in_block(next_frame);
		frame_unwind_register(next_frame, I386_ESP_REGNUM, buf);
		cache->sp = extract_unsigned_integer(buf,
		    register_size(gdbarch, I386_ESP_REGNUM),
		    byte_order);
		cache->tss = kgdb_trgt_fetch_tss();
	}
	return (cache);
}

static void
kgdb_trgt_dblfault_this_id(struct frame_info *next_frame, void **this_cache,
    struct frame_id *this_id)
{
	struct kgdb_tss_cache *cache;

	cache = kgdb_trgt_tss_cache(next_frame, this_cache);
	*this_id = frame_id_build(cache->sp, cache->pc);
}

static struct value *
kgdb_trgt_dblfault_prev_register(struct frame_info *next_frame,
    void **this_cache, int regnum)
{
	CORE_ADDR addrp;
	struct kgdb_tss_cache *cache;
	int ofs;

	if (regnum < I386_EAX_REGNUM || regnum > I386_FS_REGNUM)
		return frame_unwind_got_register(next_frame, regnum, regnum);

	ofs = kgdb_trgt_tss_offset[regnum];

	cache = kgdb_trgt_tss_cache(next_frame, this_cache);
	if (cache->tss == 0)
		return frame_unwind_got_register(next_frame, regnum, regnum);

	addrp = cache->tss + ofs;
	return frame_unwind_got_memory(next_frame, regnum, addrp);
}

static enum unwind_stop_reason
kgdb_trgt_dblfault_unwind_reason(struct frame_info *next_frame,
    void **this_cache)
{
    /* XXX marino : populate logic to determine unwind stoppage */
    return UNWIND_NO_REASON;
}

static const struct frame_unwind kgdb_trgt_dblfault_unwind = {
        NORMAL_FRAME,
        &kgdb_trgt_dblfault_unwind_reason,
        &kgdb_trgt_dblfault_this_id,
        &kgdb_trgt_dblfault_prev_register,
	.sniffer = kgdb_trgt_trapframe_sniffer
};

struct kgdb_frame_cache {
	int		frame_type;
	CORE_ADDR	pc;
	CORE_ADDR	sp;
};
#define	FT_NORMAL		1
#define	FT_INTRFRAME		2
/*#define	FT_INTRTRAPFRAME	3*/
#define	FT_TIMERFRAME		4
#define	FT_CALLTRAP		5

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
		/* else if (strcmp(pname, "Xcpustop") == 0 ||
		    strcmp(pname, "Xrendezvous") == 0 ||
		    strcmp(pname, "Xipi_intr_bitmap_handler") == 0 ||
		    strcmp(pname, "Xlazypmap") == 0)
			cache->frame_type = FT_INTRTRAPFRAME;
			*/
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

	if (regnum < I386_EAX_REGNUM || regnum > I386_FS_REGNUM)
		return frame_unwind_got_register(next_frame, regnum, regnum);

	ofs = kgdb_trgt_frame_offset[regnum] + 4;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);

	switch (cache->frame_type) {
	case FT_NORMAL:
		break;
	case FT_INTRFRAME:
		ofs += 4;
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

#if 0
	/*
	 * If we are in the kernel, we don't have esp stored in the
	 * trapframe, but we can calculate it simply by subtracting
	 * the size of the frame.
	 */
	if (regnum == I386_ESP_REGNUM) {
		char buf[4];

		frame_unwind_register(next_frame, I386_CS_REGNUM, buf);
		if (extract_unsigned_integer(buf, 4, byte_order) != SEL_UPL)
			return frame_unwind_got_address(next_frame, regnum, addrp);
		/* else FALLTHROUGH */
	}
#endif

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

	/*
	 * This is a combined sniffer, since only the
	 * function names change.
	 */

	/*
	 * If we're the sniffer for a trapframe, deal with
	 * all these function names.
	 */
	if (self == &kgdb_trgt_trapframe_unwind &&
	    (strcmp(pname, "calltrap") == 0 ||
	     (pname[0] == 'X' && pname[1] != '_')))
		return (1);

	/*
	 * If we're a double fault sniffer, only look for
	 * the double fault name.
	 */
	if(self == &kgdb_trgt_dblfault_unwind &&
	   strcmp(pname, "dblfault_handler") == 0)
		return (1);

	/* printf("%s: %llx =%s\n", __func__, pc, pname); */
	return (0);
}
