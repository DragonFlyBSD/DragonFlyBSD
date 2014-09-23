/*
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

static int db_dr(struct db_variable *vp, db_expr_t *valuep, int op);

/*
 * Machine register set.
 */
struct db_variable db_regs[] = {
	{ "cs",		&ddb_regs.tf_cs,     NULL },
	{ "ds",		&ddb_regs.tf_ds,     NULL },
	{ "es",		&ddb_regs.tf_es,     NULL },
	{ "fs",		&ddb_regs.tf_fs,     NULL },
	{ "gs",		&ddb_regs.tf_gs,     NULL },
	{ "ss",		&ddb_regs.tf_ss,     NULL },
	{ "eax",	&ddb_regs.tf_eax,    NULL },
	{ "ecx",	&ddb_regs.tf_ecx,    NULL },
	{ "edx",	&ddb_regs.tf_edx,    NULL },
	{ "ebx",	&ddb_regs.tf_ebx,    NULL },
	{ "esp",	&ddb_regs.tf_esp,    NULL },
	{ "ebp",	&ddb_regs.tf_ebp,    NULL },
	{ "esi",	&ddb_regs.tf_esi,    NULL },
	{ "edi",	&ddb_regs.tf_edi,    NULL },
	{ "eip",	&ddb_regs.tf_eip,    NULL },
	{ "efl",	&ddb_regs.tf_eflags, NULL },
	{ "dr0",	NULL,		     db_dr },
	{ "dr1",	NULL,		     db_dr },
	{ "dr2",	NULL,		     db_dr },
	{ "dr3",	NULL,		     db_dr },
	{ "dr4",	NULL,		     db_dr },
	{ "dr5",	NULL,		     db_dr },
	{ "dr6",	NULL,		     db_dr },
	{ "dr7",	NULL,		     db_dr },
};
struct db_variable *db_eregs = db_regs + NELEM(db_regs);

/*
 * Stack trace.
 */
#define	INKERNEL(va)	(((vm_offset_t)(va)) >= USRSTACK)

struct i386_frame {
	struct i386_frame	*f_frame;
	int			f_retaddr;
	int			f_arg0;
};

#define NORMAL		0
#define	TRAP		1
#define	INTERRUPT	2
#define	SYSCALL		3

static void	db_nextframe(struct i386_frame **, db_addr_t *);
static int	db_numargs(struct i386_frame *);
static void	db_print_stack_entry(const char *, int, char **, int *, db_addr_t);
static void	dl_symbol_values(int callpc, const char **name);


int		db_md_set_watchpoint(db_expr_t addr, db_expr_t size);
int		db_md_clr_watchpoint(db_expr_t addr, db_expr_t size);
void		db_md_list_watchpoints(void);


/*
 * Figure out how many arguments were passed into the frame at "fp".
 */
static int
db_numargs(struct i386_frame *fp)
{
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
}

static void
db_print_stack_entry(const char *name, int narg, char **argnp, int *argp,
		     db_addr_t callpc)
{
	db_printf("%s(", name);
	while (narg) {
		if (argnp)
			db_printf("%s=", *argnp++);
		db_printf("%r", db_get_value((int)argp, 4, FALSE));
		argp++;
		if (--narg != 0)
			db_printf(",");
  	}
	db_printf(") at ");
	db_printsym(callpc, DB_STGY_PROC);
	db_printf("\n");
}

/*
 * Figure out the next frame up in the call stack.
 */
static void
db_nextframe(struct i386_frame **fp, db_addr_t *ip)
{
	struct trapframe *tf;
	int frame_type;
	int eip, esp, ebp;
	db_expr_t offset;
	const char *sym, *name;

	eip = db_get_value((int) &(*fp)->f_retaddr, 4, FALSE);
	ebp = db_get_value((int) &(*fp)->f_frame, 4, FALSE);

	/*
	 * Figure out frame type.
	 */

	frame_type = NORMAL;

	sym = db_search_symbol(eip, DB_STGY_ANY, &offset);
	db_symbol_values(sym, &name, NULL);
	dl_symbol_values(eip, &name);
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
		*ip = (db_addr_t) eip;
		*fp = (struct i386_frame *) ebp;
		return;
	}

	db_print_stack_entry(name, 0, 0, 0, eip);

	/*
	 * Point to base of trapframe which is just above the
	 * current frame.
	 */
	tf = (struct trapframe *) ((int)*fp + 8);

#if 0
	esp = (ISPL(tf->tf_cs) == SEL_UPL) ?  tf->tf_esp : (int)&tf->tf_esp;
#endif
	esp = (int)&tf->tf_esp;

	switch (frame_type) {
	case TRAP:
		{
			eip = tf->tf_eip;
			ebp = tf->tf_ebp;
			db_printf(
		    "--- trap %#r, eip = %#r, esp = %#r, ebp = %#r ---\n",
			    tf->tf_trapno, eip, esp, ebp);
		}
		break;
	case SYSCALL:
		{
			eip = tf->tf_eip;
			ebp = tf->tf_ebp;
			db_printf(
		    "--- syscall %#r, eip = %#r, esp = %#r, ebp = %#r ---\n",
			    tf->tf_eax, eip, esp, ebp);
		}
		break;
	case INTERRUPT:
		tf = (struct trapframe *)((int)*fp + 16);
		{
			eip = tf->tf_eip;
			ebp = tf->tf_ebp;
			db_printf(
		    "--- interrupt, eip = %#r, esp = %#r, ebp = %#r ---\n",
			    eip, esp, ebp);
		}
		break;
	default:
		break;
	}

	*ip = (db_addr_t) eip;
	*fp = (struct i386_frame *) ebp;
}

void
db_stack_trace_cmd(db_expr_t addr, boolean_t have_addr, db_expr_t count,
		   char *modif)
{
	struct i386_frame *frame;
	int *argp;
	db_addr_t callpc;
	boolean_t first;
	int i;

	if (count == -1)
		count = 1024;

	if (!have_addr) {
		frame = (struct i386_frame *)BP_REGS(&ddb_regs);
		if (frame == NULL)
			frame = (struct i386_frame *)(SP_REGS(&ddb_regs) - 4);
		callpc = PC_REGS(&ddb_regs);
	} else {
		/*
		 * Look for something that might be a frame pointer, just as
		 * a convenience.
		 */
		frame = (struct i386_frame *)addr;
		for (i = 0; i < 4096; i += 4) {
			struct i386_frame *check;

			check = (struct i386_frame *)db_get_value((int)((char *)&frame->f_frame + i), 4, FALSE);
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
		callpc = (db_addr_t)db_get_value((int)&frame->f_retaddr, 4, FALSE);
	}

	first = TRUE;
	while (count--) {
		struct i386_frame *actframe;
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
				if ((instr & 0x00ffffff) == 0x00e58955) {
					/* pushl %ebp; movl %esp, %ebp */
					actframe = (struct i386_frame *)
					    (SP_REGS(&ddb_regs) - 4);
				} else if ((instr & 0x0000ffff) == 0x0000e589) {
					/* movl %esp, %ebp */
					actframe = (struct i386_frame *)
					    SP_REGS(&ddb_regs);
					if (ddb_regs.tf_ebp == 0) {
						/* Fake caller's frame better. */
						frame = actframe;
					}
				} else if ((instr & 0x000000ff) == 0x000000c3) {
					/* ret */
					actframe = (struct i386_frame *)
					    (SP_REGS(&ddb_regs) - 4);
				} else if (offset == 0) {
					/* Probably a symbol in assembler code. */
					actframe = (struct i386_frame *)
					    (SP_REGS(&ddb_regs) - 4);
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

		if (actframe != frame) {
			/* `frame' belongs to caller. */
			callpc = (db_addr_t)
			    db_get_value((int)&actframe->f_retaddr, 4, FALSE);
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
	register_t  ebp;

	__asm __volatile("movl %%ebp, %0" : "=r" (ebp));
	db_stack_trace_cmd(ebp, 1, count, NULL);
}

static int
db_dr(struct db_variable *vp, db_expr_t *valuep, int op)
{
	if (op == DB_VAR_GET)
		*valuep = 0;
	return(-1);
}

int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{
	return(-1);
}

int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{
	return(-1);
}

void
db_md_list_watchpoints(void)
{
	/* no hardware watchpoints in vkernel */
}

/*
 * See if dladdr() can get the symbol name via the standard dynamic loader.
 */
static
void
dl_symbol_values(int callpc, const char **name)
{
	Dl_info info;

	if (*name == NULL) {
		if (dladdr((const void *)callpc, &info) != 0) {
			if (info.dli_saddr <= (const void *)callpc)
				*name = info.dli_sname;
		}
	}
}
