/*-
 * Copyright (c) 1993, Garrett A. Wollman.
 * Copyright (c) 1993, University of Vermont and State Agricultural College.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/ddb/ddb.h,v 1.24.2.2 2002/08/30 22:27:49 gibbs Exp $
 */

/*
 * Necessary declarations for the `ddb' kernel debugger.
 */

#ifndef _DDB_DDB_H_
#define	_DDB_DDB_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

#include <machine/db_machdep.h>		/* type definitions */

#ifndef _MACHINE_GLOBALDATA_H_
#include <machine/globaldata.h>
#endif

typedef void db_cmdfcn_t (db_expr_t addr, boolean_t have_addr,
			      db_expr_t count, char *modif);

#define DB_COMMAND_FLAG(cmd_name, func_name, flag) \
	DB_SET(cmd_name, func_name, db_cmd_set, flag, NULL)
#define DB_COMMAND(cmd_name, func_name) \
	DB_COMMAND_FLAG(cmd_name, func_name, 0)
#define DB_SHOW_COMMAND(cmd_name, func_name) \
	DB_SET(cmd_name, func_name, db_show_cmd_set, 0, NULL)

#define DB_SET(cmd_name, func_name, set, flag, more)		\
static db_cmdfcn_t	func_name;				\
								\
static const struct command __CONCAT(func_name,_cmd) = {	\
	__STRING(cmd_name),					\
	func_name,						\
	flag,							\
	more							\
};								\
TEXT_SET(set, __CONCAT(func_name,_cmd));			\
								\
static void							\
func_name(db_expr_t addr, boolean_t have_addr, db_expr_t count, \
	  char *modif)

extern char *esym;
extern db_expr_t db_maxoff;
extern int db_active;
extern int db_indent;
extern int db_inst_count;
extern int db_load_count;
extern int debugger_on_panic;
extern int db_store_count;
extern db_expr_t db_radix;
extern db_expr_t db_max_width;
extern db_expr_t db_tab_stop_width;

struct vm_map;

int		db_more(int *nl);
void		db_check_interrupt (void);
void		db_clear_watchpoints (void);
db_addr_t	db_disasm (db_addr_t loc, boolean_t altfmt, db_regs_t *regs);
				/* instruction disassembler */
void		db_error (char *s);
int		db_expression (db_expr_t *valuep);
int		db_get_variable (db_expr_t *valuep);
void		db_iprintf (const char *,...) __printflike(1, 2);
struct vm_map	*db_map_addr (vm_offset_t);
boolean_t	db_map_current (struct vm_map *);
boolean_t	db_map_equal (struct vm_map *, struct vm_map *);
void		db_print_loc_and_inst (db_addr_t loc, db_regs_t *regs);
void		db_printf (const char *fmt, ...) __printflike(1, 2);
void		db_vprintf (const char *fmt, __va_list va) __printflike(1, 0);
void		db_read_bytes (vm_offset_t addr, size_t size, char *data);
				/* machine-dependent */
int		db_readline (char *lstart, int lsize);
void		db_restart_at_pc (boolean_t watchpt);
void		db_set_watchpoints (void);
void		db_skip_to_eol (void);
boolean_t	db_stop_at_pc (boolean_t *is_breakpoint);
#define		db_strcpy	strcpy
void		db_trap (int type, int code);
boolean_t	db_value_of_name (const char *name, db_expr_t *valuep);
void		db_write_bytes (vm_offset_t addr, size_t size, char *data);
				/* machine-dependent */
void		kdb_init (void);

db_cmdfcn_t	db_breakpoint_cmd;
db_cmdfcn_t	db_continue_cmd;
db_cmdfcn_t	db_delete_cmd;
db_cmdfcn_t	db_deletehwatch_cmd;
db_cmdfcn_t	db_deletewatch_cmd;
db_cmdfcn_t	db_examine_cmd;
db_cmdfcn_t	db_hwatchpoint_cmd;
db_cmdfcn_t	db_listbreak_cmd;
db_cmdfcn_t	db_print_cmd;
db_cmdfcn_t	db_ps;
db_cmdfcn_t	db_search_cmd;
db_cmdfcn_t	db_set_cmd;
db_cmdfcn_t	db_show_regs;
db_cmdfcn_t	db_single_step_cmd;
db_cmdfcn_t	db_stack_trace_cmd;
db_cmdfcn_t	db_trace_until_call_cmd;
db_cmdfcn_t	db_trace_until_matching_cmd;
db_cmdfcn_t	db_watchpoint_cmd;
db_cmdfcn_t	db_write_cmd;

#if 0
db_cmdfcn_t	db_help_cmd;
db_cmdfcn_t	db_show_all_threads;
db_cmdfcn_t	db_show_one_thread;
db_cmdfcn_t	ipc_port_print;
db_cmdfcn_t	vm_page_print;
#endif

/*
 * Command table.
 */
struct command {
	char *	name;		/* command name */
	db_cmdfcn_t *fcn;	/* function to call */
	int	flag;		/* extra info: */
#define	CS_OWN		0x1	/* non-standard syntax */
#define	CS_MORE		0x2	/* standard syntax, but may have other words
				 * at end */
#define	CS_SET_DOT	0x100	/* set dot after command */
	struct command *more;	/* another level of command */
};

#endif /* !_DDB_DDB_H_ */
