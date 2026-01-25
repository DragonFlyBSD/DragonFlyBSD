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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/amd64/include/db_machdep.h,v 1.22 2005/01/05 20:17:20 imp Exp $
 */

#ifndef _CPU_DB_MACHDEP_H_
#define	_CPU_DB_MACHDEP_H_

typedef unsigned long db_addr_t;
typedef long db_expr_t;

typedef struct db_regs {
	unsigned long	dummy;
} db_regs_t;

#ifdef _KERNEL
#define	DDB_EXPR_FMT	"l"
extern db_regs_t ddb_regs;
#define DDB_REGS	(&ddb_regs)

static __inline db_addr_t
PC_REGS(db_regs_t *regs)
{
	(void)regs;
	return (0);
}

static __inline db_addr_t
SP_REGS(db_regs_t *regs)
{
	(void)regs;
	return (0);
}

static __inline db_addr_t
BP_REGS(db_regs_t *regs)
{
	(void)regs;
	return (0);
}

#endif

#define	BKPT_INST	0
#define	BKPT_SIZE	1
#define	BKPT_SET(inst)	(BKPT_INST)

#define	FIXUP_PC_AFTER_BREAK

#define	db_clear_single_step(regs)	(void)(regs)
#define	db_set_single_step(regs)	(void)(regs)

#define	IS_BREAKPOINT_TRAP(type, code)	0
#define	IS_WATCHPOINT_TRAP(type, code)	0

#define	inst_trap_return(ins)	0
#define	inst_return(ins)	0
#define	inst_call(ins)	0
#define	inst_load(ins)	0
#define	inst_store(ins)	0

#define	DB_SMALL_VALUE_MAX	0x7fffffff
#define	DB_SMALL_VALUE_MIN	(-0x400001)

#endif /* !_CPU_DB_MACHDEP_H_ */
