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
 * $FreeBSD: src/sys/ddb/db_print.c,v 1.26 1999/08/28 00:41:09 peter Exp $
 */

/*
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

/*
 * Miscellaneous printing.
 */
#include <sys/param.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_sym.h>

void
db_show_regs(db_expr_t dummy1, boolean_t dummy2, db_expr_t dummy3, char *dummy4)
{
	struct db_variable *regp;
	db_expr_t	value, offset;
	const char *	name;

	for (regp = db_regs; regp < db_eregs; regp++) {
	    db_read_variable(regp, &value);
	    db_printf("%-12s%s", regp->name, db_num_to_str(value));
	    db_find_xtrn_sym_and_offset((db_addr_t)value, &name, &offset);
	    if (name != NULL && offset <= (unsigned long)db_maxoff &&
		offset != value) {
		db_printf("\t%s", name);
		if (offset != 0) {
		    char tbuf[24];

		    db_format_radix(tbuf, 24, offset, TRUE);
		    db_printf("+%s", tbuf);
		}
	    }
	    db_printf("\n");
	}
	db_print_loc_and_inst(PC_REGS(DDB_REGS), DDB_REGS);
}
