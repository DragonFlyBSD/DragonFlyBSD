/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * --
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/i386/i386/db_trace.c,v 1.35.2.3 2002/02/21 22:31:25 silby Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker_set.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/reg.h>

#include <machine/cpu.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <ddb/ddb.h>
#include <dlfcn.h>	/* DLL */

#include <sys/user.h>

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>

db_varfcn_t db_dr0;
db_varfcn_t db_dr1;
db_varfcn_t db_dr2;
db_varfcn_t db_dr3;
db_varfcn_t db_dr4;
db_varfcn_t db_dr5;
db_varfcn_t db_dr6;
db_varfcn_t db_dr7;

/*
 * Machine register set.
 */
struct db_variable db_regs[] = {
	{ "cs",		&ddb_regs.tf_cs,     NULL },
/*	{ "ds",		&ddb_regs.tf_ds,     NULL },
	{ "es",		&ddb_regs.tf_es,     NULL },
	{ "fs",		&ddb_regs.tf_fs,     NULL },
	{ "gs",		&ddb_regs.tf_gs,     NULL }, */
	{ "ss",		&ddb_regs.tf_ss,     NULL },
	{ "rax",	&ddb_regs.tf_rax,    NULL },
	{ "rcx",	&ddb_regs.tf_rcx,    NULL },
	{ "rdx",	&ddb_regs.tf_rdx,    NULL },
	{ "rbx",	&ddb_regs.tf_rbx,    NULL },
	{ "rsp",	&ddb_regs.tf_rsp,    NULL },
	{ "rbp",	&ddb_regs.tf_rbp,    NULL },
	{ "rsi",	&ddb_regs.tf_rsi,    NULL },
	{ "rdi",	&ddb_regs.tf_rdi,    NULL },
	{ "rip",	&ddb_regs.tf_rip,    NULL },
	{ "rfl",	&ddb_regs.tf_rflags, NULL },
	{ "r8",		&ddb_regs.tf_r8,     NULL },
	{ "r9",		&ddb_regs.tf_r9,     NULL },
	{ "r10",	&ddb_regs.tf_r10,    NULL },
	{ "r11",	&ddb_regs.tf_r11,    NULL },
	{ "r12",	&ddb_regs.tf_r12,    NULL },
	{ "r13",	&ddb_regs.tf_r13,    NULL },
	{ "r14",	&ddb_regs.tf_r14,    NULL },
	{ "r15",	&ddb_regs.tf_r15,    NULL },
	{ "dr0",	NULL,		     db_dr0 },
	{ "dr1",	NULL,		     db_dr1 },
	{ "dr2",	NULL,		     db_dr2 },
	{ "dr3",	NULL,		     db_dr3 },
	{ "dr4",	NULL,		     db_dr4 },
	{ "dr5",	NULL,		     db_dr5 },
	{ "dr6",	NULL,		     db_dr6 },
	{ "dr7",	NULL,		     db_dr7 },
};
struct db_variable *db_eregs = db_regs + NELEM(db_regs);

/*
 * Stack trace.
 */
#define	INKERNEL(va)	(((vm_offset_t)(va)) >= USRSTACK)

struct x86_64_frame {
	struct x86_64_frame	*f_frame;
	long			f_retaddr;
	long			f_arg0;
};

#define NORMAL		0
#define	TRAP		1
#define	INTERRUPT	2
#define	SYSCALL		3

static void	db_nextframe(struct x86_64_frame **, db_addr_t *);
static int	db_numargs(struct x86_64_frame *);
static void	db_print_stack_entry(const char *, int, char **, long *, db_addr_t);
static void	dl_symbol_values(long callpc, const char **name);


static char	*watchtype_str(int type);
static int	kx86_64_set_watch(int watchnum, unsigned int watchaddr, 
                               int size, int access, struct dbreg * d);
static int	kx86_64_clr_watch(int watchnum, struct dbreg * d);
int		db_md_set_watchpoint(db_expr_t addr, db_expr_t size);
int		db_md_clr_watchpoint(db_expr_t addr, db_expr_t size);
void		db_md_list_watchpoints(void);


/*
 * Figure out how many arguments were passed into the frame at "fp".
 */
static int
db_numargs(struct x86_64_frame *fp)
{
#if 1
	return (0);	/* regparm, needs dwarf2 info */
#else
	int	args;
#if 0
	int	*argp;
	int	inst;

	argp = (int *)db_get_value((int)&fp->f_retaddr, 4, FALSE);
	/*
	 * XXX etext is wrong for LKMs.  We should attempt to interpret
	 * the instruction at the return address in all cases.  This
	 * may require better fault handling.
	 */
	if (argp < (int *)btext || argp >= (int *)etext) {
		args = 5;
	} else {
		inst = db_get_value((int)argp, 4, FALSE);
		if ((inst & 0xff) == 0x59)	/* popl %ecx */
			args = 1;
		else if ((inst & 0xffff) == 0xc483)	/* addl $Ibs, %esp */
			args = ((inst >> 16) & 0xff) / 4;
		else
			args = 5;
	}
#endif
	args = 5;
	return(args);
#endif
}

static void
db_print_stack_entry(const char *name, int narg, char **argnp, long *argp,
		     db_addr_t callpc)
{
	db_printf("%s(", name);
	while (narg) {
		if (argnp)
			db_printf("%s=", *argnp++);
		db_printf("%ld", (long)db_get_value((long)argp, 8, FALSE));
		argp++;
		if (--narg != 0)
			db_printf(",");
	}
	db_printf(") at ");
	db_printsym(callpc, DB_STGY_PROC);
	db_printf(" %p ",  (void*) callpc);
	db_printf("\n");
}

/*
 * Figure out the next frame up in the call stack.
 */
static void
db_nextframe(struct x86_64_frame **fp, db_addr_t *ip)
{
	struct trapframe *tf;
	int frame_type;
	long rip, rsp, rbp;
	db_expr_t offset;
	const char *sym, *name;

	if ((unsigned long)*fp < PAGE_SIZE) {
		*fp = NULL;
		return;
	}
	rip = db_get_value((long) &(*fp)->f_retaddr, 8, FALSE);
	rbp = db_get_value((long) &(*fp)->f_frame, 8, FALSE);

	/*
	 * Figure out frame type.
	 */

	frame_type = NORMAL;

	sym = db_search_symbol(rip, DB_STGY_ANY, &offset);
	db_symbol_values(sym, &name, NULL);
	dl_symbol_values(rip, &name);
	if (name != NULL) {
		if (!strcmp(name, "calltrap")) {
			frame_type = TRAP;
		} else if (!strncmp(name, "Xresume", 7)) {
			frame_type = INTERRUPT;
		} else if (!strcmp(name, "_Xsyscall")) {
			frame_type = SYSCALL;
		}
	}

	/*
	 * Normal frames need no special processing.
	 */
	if (frame_type == NORMAL) {
		*ip = (db_addr_t) rip;
		*fp = (struct x86_64_frame *) rbp;
		return;
	}

	db_print_stack_entry(name, 0, 0, 0, rip);

	/*
	 * Point to base of trapframe which is just above the
	 * current frame.
	 */
	tf = (struct trapframe *)((long)*fp + 16);

#if 0
	rsp = (ISPL(tf->tf_cs) == SEL_UPL) ?  tf->tf_rsp : (long)&tf->tf_rsp;
#endif
	rsp = (long)&tf->tf_rsp;

	switch (frame_type) {
	case TRAP:
		{
			rip = tf->tf_rip;
			rbp = tf->tf_rbp;
			db_printf(
	    "--- trap %016lx, rip = %016lx, rsp = %016lx, rbp = %016lx ---\n",
			    tf->tf_trapno, rip, rsp, rbp);
		}
		break;
	case SYSCALL:
		{
			rip = tf->tf_rip;
			rbp = tf->tf_rbp;
			db_printf(
	"--- syscall %016lx, rip = %016lx, rsp = %016lx, rbp = %016lx ---\n",
			    tf->tf_rax, rip, rsp, rbp);
		}
		break;
	case INTERRUPT:
		tf = (struct trapframe *)((long)*fp + 16);
		{
			rip = tf->tf_rip;
			rbp = tf->tf_rbp;
			db_printf(
	    "--- interrupt, rip = %016lx, rsp = %016lx, rbp = %016lx ---\n",
			    rip, rsp, rbp);
		}
		break;
	default:
		break;
	}

	*ip = (db_addr_t) rip;
	*fp = (struct x86_64_frame *) rbp;
}

void
db_stack_trace_cmd(db_expr_t addr, boolean_t have_addr, db_expr_t count,
		   char *modif)
{
	struct x86_64_frame *frame;
	long *argp;
	db_addr_t callpc;
	boolean_t first;
	int i;

	if (count == -1)
		count = 1024;

	if (!have_addr) {
		frame = (struct x86_64_frame *)BP_REGS(&ddb_regs);
		if (frame == NULL)
			frame = (struct x86_64_frame *)(SP_REGS(&ddb_regs) - 8);
		callpc = PC_REGS(&ddb_regs);
	} else {
		/*
		 * Look for something that might be a frame pointer, just as
		 * a convenience.
		 */
		frame = (struct x86_64_frame *)addr;
		for (i = 0; i < 4096; i += 8) {
			struct x86_64_frame *check;

			check = (struct x86_64_frame *)db_get_value((long)((char *)&frame->f_frame + i), 8, FALSE);
			if ((char *)check - (char *)frame >= 0 &&
			    (char *)check - (char *)frame < 4096
			) {
				break;
			}
			db_printf("%p does not look like a stack frame, skipping\n", (char *)&frame->f_frame + i);
		}
		if (i == 4096) {
			db_printf("Unable to find anything that looks like a stack frame\n");
			return;
		}
		frame = (void *)((char *)frame + i);
		db_printf("Trace beginning at frame %p\n", frame);
		callpc = (db_addr_t)db_get_value((long)&frame->f_retaddr, 8, FALSE);
	}

	first = TRUE;
	while (count--) {
		struct x86_64_frame *actframe;
		int		narg;
		const char *	name;
		db_expr_t	offset;
		c_db_sym_t	sym;
#define MAXNARG	16
		char	*argnames[MAXNARG], **argnp = NULL;

		sym = db_search_symbol(callpc, DB_STGY_ANY, &offset);
		db_symbol_values(sym, &name, NULL);
		dl_symbol_values(callpc, &name);

		/*
		 * Attempt to determine a (possibly fake) frame that gives
		 * the caller's pc.  It may differ from `frame' if the
		 * current function never sets up a standard frame or hasn't
		 * set one up yet or has just discarded one.  The last two
		 * cases can be guessed fairly reliably for code generated
		 * by gcc.  The first case is too much trouble to handle in
		 * general because the amount of junk on the stack depends
		 * on the pc (the special handling of "calltrap", etc. in
		 * db_nextframe() works because the `next' pc is special).
		 */
		actframe = frame;
		if (first) {
			if (!have_addr) {
				int instr;

				instr = db_get_value(callpc, 4, FALSE);
				if ((instr & 0xffffffff) == 0xe5894855) {
					/* pushq %rbp; movq %rsp, %rbp */
					actframe = (struct x86_64_frame *)
					    (SP_REGS(&ddb_regs) - 8);
				} else if ((instr & 0xffffff) == 0xe58948) {
					/* movq %rsp, %rbp */
					actframe = (struct x86_64_frame *)
					    SP_REGS(&ddb_regs);
					if (ddb_regs.tf_rbp == 0) {
						/* Fake caller's frame better. */
						frame = actframe;
					}
				} else if ((instr & 0xff) == 0xc3) {
					/* ret */
					actframe = (struct x86_64_frame *)
					    (SP_REGS(&ddb_regs) - 8);
				} else if (offset == 0) {
					/* Probably a symbol in assembler code. */
					actframe = (struct x86_64_frame *)
					    (SP_REGS(&ddb_regs) - 8);
				}
			} else if (name != NULL &&
				   strcmp(name, "fork_trampoline") == 0) {
				/*
				 * Don't try to walk back on a stack for a
				 * process that hasn't actually been run yet.
				 */
				db_print_stack_entry(name, 0, 0, 0, callpc);
				break;
			}
			first = FALSE;
		}

		argp = &actframe->f_arg0;
		narg = MAXNARG;
		if (sym != NULL && db_sym_numargs(sym, &narg, argnames)) {
			argnp = argnames;
		} else {
			narg = db_numargs(frame);
		}

		db_print_stack_entry(name, narg, argnp, argp, callpc);

		/*
		 * Stop at the system call boundary (else we risk
		 * double-faulting on junk).
		 */
		if (name && strcmp(name, "Xfast_syscall") == 0)
			break;

		if (actframe != frame) {
			/* `frame' belongs to caller. */
			callpc = (db_addr_t)
			    db_get_value((long)&actframe->f_retaddr, 8, FALSE);
			continue;
		}

		db_nextframe(&frame, &callpc);
		if (frame == NULL)
			break;
	}
}

void
print_backtrace(int count)
{
	register_t  rbp;

	__asm __volatile("movq %%rbp, %0" : "=r" (rbp));
	db_stack_trace_cmd(rbp, 1, count, NULL);
}

#define DB_DRX_FUNC(reg)						\
int									\
db_ ## reg (struct db_variable *vp, db_expr_t *valuep, int op)		\
{									\
	if (op == DB_VAR_GET)						\
		*valuep = r ## reg ();					\
	else								\
		load_ ## reg (*valuep); 				\
									\
	return(0);							\
} 

DB_DRX_FUNC(dr0)
DB_DRX_FUNC(dr1)
DB_DRX_FUNC(dr2)
DB_DRX_FUNC(dr3)
DB_DRX_FUNC(dr4)
DB_DRX_FUNC(dr5)
DB_DRX_FUNC(dr6)
DB_DRX_FUNC(dr7)

static int
kx86_64_set_watch(int watchnum, unsigned int watchaddr, int size, int access,
	       struct dbreg *d)
{
	int i;
	unsigned int mask;
	
	if (watchnum == -1) {
		for (i = 0, mask = 0x3; i < 4; i++, mask <<= 2)
			if ((d->dr[7] & mask) == 0)
				break;
		if (i < 4)
			watchnum = i;
		else
			return(-1);
	}

	switch (access) {
	case DBREG_DR7_EXEC:
		size = 1; /* size must be 1 for an execution breakpoint */
		/* fall through */
	case DBREG_DR7_WRONLY:
	case DBREG_DR7_RDWR:
		break;
	default:
		return(-1);
	}

	/*
	 * we can watch a 1, 2, 4, or 8 byte sized location
	 */
	switch (size) {
	case 1:
		mask = 0x00;
		break;
	case 2:
		mask = 0x01 << 2;
		break;
	case 4:
		mask = 0x03 << 2;
		break;
	case 8:
		mask = 0x02 << 2;
		break;
	default:
		return(-1);
	}

	mask |= access;

	/* clear the bits we are about to affect */
	d->dr[7] &= ~((0x3 << (watchnum * 2)) | (0x0f << (watchnum * 4 + 16)));

	/* set drN register to the address, N=watchnum */
	DBREG_DRX(d, watchnum) = watchaddr;

	/* enable the watchpoint */
	d->dr[7] |= (0x2 << (watchnum * 2)) | (mask << (watchnum * 4 + 16));

	return(watchnum);
}


int
kx86_64_clr_watch(int watchnum, struct dbreg *d)
{
	if (watchnum < 0 || watchnum >= 4)
		return(-1);

	d->dr[7] &= ~((0x3 << (watchnum * 2)) | (0x0f << (watchnum * 4 + 16)));
	DBREG_DRX(d, watchnum) = 0;

	return(0);
}


int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{
	int avail, wsize;
	int i;
	struct dbreg d;
	
	fill_dbregs(NULL, &d);

	avail = 0;
	for (i = 0; i < 4; i++) {
		if ((d.dr[7] & (3 << (i * 2))) == 0)
			avail++;
	}

	if (avail * 8 < size)
		return(-1);

	for (i=0; i < 4 && (size != 0); i++) {
		if ((d.dr[7] & (3 << (i * 2))) == 0) {
			if (size >= 8 || (avail == 1 && size > 4))
				wsize = 8;
			else if (size > 2)
				wsize = 4;
			else
				wsize = size;
			if (wsize == 3)
				wsize++;
			kx86_64_set_watch(i, addr, wsize, DBREG_DR7_WRONLY, &d);
			addr += wsize;
			size -= wsize;
		}
	}

	set_dbregs(NULL, &d);

	return(0);
}

int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{
	struct dbreg d;
	int i;

	fill_dbregs(NULL, &d);

	for(i = 0; i < 4; i++) {
		if (d.dr[7] & (3 << (i * 2))) {
			if ((DBREG_DRX((&d), i) >= addr) && 
			    (DBREG_DRX((&d), i) < addr + size))
				kx86_64_clr_watch(i, &d);
		}
	}

	set_dbregs(NULL, &d);

	return(0);
}

static char *
watchtype_str(int type)
{
	switch (type) {
	case DBREG_DR7_EXEC:
		return "execute";
	case DBREG_DR7_RDWR:
		return "read/write";
	case DBREG_DR7_WRONLY:
		return "write";
	default:
		return "invalid";
	}
}

void
db_md_list_watchpoints(void)
{
	int i;
	struct dbreg d;

	fill_dbregs(NULL, &d);

	db_printf("\nhardware watchpoints:\n");
	db_printf("  watch    status        type  len     address\n"
		  "  -----  --------  ----------  ---  ----------\n");
	for (i = 0; i < 4; i++) {
		if (d.dr[7] & (0x03 << (i * 2))) {
			unsigned type, len;
			type = (d.dr[7] >> (16 + (i * 4))) & 3;
			len =  (d.dr[7] >> (16 + (i * 4) + 2)) & 3;
			db_printf("  %-5d  %-8s  %10s  %3d  0x%08lx\n",
				  i, "enabled", watchtype_str(type), 
				  len + 1, DBREG_DRX((&d), i));
		} else {
			db_printf("  %-5d  disabled\n", i);
		}
	}

	db_printf("\ndebug register values:\n");
	for (i = 0; i < 8; i++)
		db_printf("  dr%d 0x%08lx\n", i, DBREG_DRX((&d),i));
	db_printf("\n");
}

/*
 * See if dladdr() can get the symbol name via the standard dynamic loader.
 */
static
void
dl_symbol_values(long callpc, const char **name)
{
#if 0
	Dl_info info;
	if (*name == NULL) {
		if (dladdr((const void *)callpc, &info) != 0) {
			if (info.dli_saddr <= (const void *)callpc)
				*name = info.dli_sname;
		}
	}
#endif
}
