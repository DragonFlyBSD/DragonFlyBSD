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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/gnu/usr.bin/gdb/kgdb/trgt.c,v 1.12 2008/05/01 20:36:48 jhb Exp $
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>

#include <defs.h>
#include <readline/readline.h>
#include <readline/tilde.h>
#include <command.h>
#include <exec.h>
#include <frame-unwind.h>
#include <gdb.h>
#include <gdbcore.h>
#include <gdbthread.h>
#include <inferior.h>
#include <language.h>
#include <regcache.h>
#include <solib.h>
#include <target.h>
#include <ui-out.h>
#include <observer.h>
#include <arch-utils.h>

#include "kgdb.h"

static void	kgdb_core_cleanup(void *);

static char *vmcore;
static struct target_ops kgdb_trgt_ops;

kvm_t *kvm;
static char kvm_err[_POSIX2_LINE_MAX];

#define	KERNOFF		(kgdb_kernbase ())
#define	INKERNEL(x)	((x) >= KERNOFF)

static CORE_ADDR
kgdb_kernbase (void)
{
	static CORE_ADDR kernbase;
	struct minimal_symbol *sym;

	if (kernbase == 0) {
		sym = lookup_minimal_symbol ("kernbase", NULL, NULL);
		if (sym == NULL) {
			kernbase = KERNBASE;
		} else {
			kernbase = SYMBOL_VALUE_ADDRESS (sym);
		}
	}
	return kernbase;
}

static void
kgdb_trgt_open(char *filename, int from_tty)
{
	struct cleanup *old_chain;
	struct kthr *kt;
	struct inferior *inf8;
	struct program_space *pspace;
	kvm_t *nkvm;
	char *temp;
	int first_inferior = 1;
	struct gdbarch_info info;
	struct gdbarch *kgdbarch;

	target_preopen (from_tty);
	if (!filename)
		error ("No vmcore file specified.");
	if (!exec_bfd)
		error ("Can't open a vmcore without a kernel");

	filename = tilde_expand (filename);
	if (filename[0] != '/') {
		temp = concat (current_directory, "/", filename, NULL);
		xfree(filename);
		filename = temp;
	}

	old_chain = make_cleanup (xfree, filename);

	nkvm = kvm_openfiles(bfd_get_filename(exec_bfd), filename, NULL,
	    write_files ? O_RDWR : O_RDONLY, kvm_err);
	if (nkvm == NULL)
		error ("Failed to open vmcore: %s", kvm_err);

	/* Don't free the filename now and close any previous vmcore. */
	discard_cleanups(old_chain);
	unpush_target(&kgdb_trgt_ops);

	kvm = nkvm;
	vmcore = filename;
	old_chain = make_cleanup(kgdb_core_cleanup, NULL);

	push_target (&kgdb_trgt_ops);
	discard_cleanups (old_chain);

	kgdb_dmesg();

	gdbarch_info_init (&info);
#if defined (__i386__)
	info.bfd_arch_info = bfd_scan_arch ("i386");
#elif defined (__x86_64__)
	info.bfd_arch_info = bfd_scan_arch ("i386:x86-64");
#else
#error platform not recognized
#endif
	info.byte_order = BFD_ENDIAN_LITTLE;
	gdbarch_info_fill (&info);
	kgdbarch = gdbarch_find_by_info (info);

	init_thread_list();
	kt = kgdb_thr_init();
	while (kt != NULL) {
		if (!in_inferior_list(kt->pid)) {
                     if (first_inferior) {
                       first_inferior = 0;
                       inf8 = current_inferior();
                       inf8->pid = kt->pid;
                       inf8->attach_flag = 1;
                       inferior_appeared (inf8, kt->pid);
                       pspace = current_program_space;
                       pspace->ebfd = 0;
                       pspace->ebfd_mtime = 0;
                     } else {                    
                       inf8 = add_inferior(kt->pid);
                       inf8->attach_flag = 0;
                       pspace = add_program_space(new_address_space());
                       pspace->symfile_object_file = symfile_objfile;
                       pspace->objfiles = object_files;
                     }
                     inf8->pspace = pspace;
                     inf8->aspace = pspace->aspace;
                     inf8->fake_pid_p = 0;
                     inf8->gdbarch = kgdbarch;
                }
		add_thread(ptid_build(kt->pid, kt->lwpid, kt->tid));
		kt = kgdb_thr_next(kt);
	}
	if (curkthr != 0)
		inferior_ptid = ptid_build(curkthr->pid, curkthr->lwpid,
			curkthr->tid);

	frame_unwind_prepend_unwinder(kgdbarch, &kgdb_trgt_trapframe_unwind);

	kld_init(kgdbarch);
	reinit_frame_cache();
	select_frame (get_current_frame());
	print_stack_frame(get_selected_frame(NULL), 0, SRC_AND_LOC);
}

static void
kgdb_trgt_close(int quitting)
{

	if (kvm != NULL) {
		inferior_ptid = null_ptid;
		clear_solib();
		if (kvm_close(kvm) != 0)
			warning("cannot close \"%s\": %s", vmcore,
			    kvm_geterr(kvm));
		kvm = NULL;
		xfree(vmcore);
		vmcore = NULL;
	}
}

static void
kgdb_core_cleanup(void *arg)
{

	kgdb_trgt_close(0);
}

static void
kgdb_trgt_detach(struct target_ops *target, char *args, int from_tty)
{

	if (args)
		error ("Too many arguments");
	unpush_target(target);
	reinit_frame_cache();
	if (from_tty)
		printf_filtered("No vmcore file now.\n");
}

static char *
kgdb_trgt_extra_thread_info(struct thread_info *ti)
{

	return (kgdb_thr_extra_thread_info(ptid_get_tid(ti->ptid)));
}

static void
kgdb_trgt_files_info(struct target_ops *target)
{

	printf_filtered ("\t`%s', ", vmcore);
	wrap_here ("        ");
	printf_filtered ("file type %s.\n", "DragonFly kernel vmcore");
}

static void
kgdb_trgt_find_new_threads(struct target_ops *target_ops)
{
	struct target_ops *tb;

	if (kvm != NULL)
		return;

	tb = find_target_beneath(target_ops);
	if (tb->to_find_new_threads != NULL)
		tb->to_find_new_threads(target_ops);
}

static char *
kgdb_trgt_pid_to_str(struct target_ops *target_ops __unused, ptid_t ptid)
{
	return (kgdb_thr_pid_to_str(ptid));
}

static int
kgdb_trgt_thread_alive(struct target_ops *target_ops __unused, ptid_t ptid)
{
	return (kgdb_thr_lookup_tid(ptid_get_tid(ptid)) != NULL);
}

static LONGEST
kgdb_trgt_xfer_partial(struct target_ops *ops, enum target_object object,
		       const char *annex, gdb_byte *readbuf,
		       const gdb_byte *writebuf,
		       ULONGEST offset, LONGEST len)
{
	if (kvm != NULL) {
		if (len == 0)
			return (0);
		if (writebuf != NULL)
			return (kvm_write(kvm, offset, writebuf, len));
		if (readbuf != NULL)
			return (kvm_read(kvm, offset, readbuf, len));
	}
	return (ops->beneath->to_xfer_partial(ops->beneath, object, annex,
					      readbuf, writebuf, offset, len));
}

static void
kgdb_switch_to_thread(struct kthr *thr)
{
	char buf[16];
	CORE_ADDR thread_id;
	char *err;

	thread_id = thr->tid;
	if (thread_id == 0)
		error ("invalid tid");
	snprintf(buf, sizeof(buf), "%lu", thread_id);
	if (!gdb_thread_select(current_uiout, buf, &err))
		error ("%s", err);
}

static void
kgdb_set_proc_cmd (char *arg, int from_tty)
{
	CORE_ADDR addr;
	struct kthr *thr;

	if (!arg)
		error_no_arg ("proc address for the new context");

	if (kvm == NULL)
		error ("only supported for core file target");

	addr = (CORE_ADDR) parse_and_eval_address (arg);

	if (!INKERNEL (addr)) {
		thr = kgdb_thr_lookup_pid((int)addr);
		if (thr == NULL)
			error ("invalid pid");
	} else {
		thr = kgdb_thr_lookup_paddr(addr);
		if (thr == NULL)
			error("invalid proc address");
	}
	kgdb_switch_to_thread(thr);
}

static void
kgdb_set_tid_cmd (char *arg, int from_tty)
{
	CORE_ADDR addr;
	struct kthr *thr;

	if (!arg)
		error_no_arg ("Thread address for the new context");

	addr = (CORE_ADDR) parse_and_eval_address (arg);
	thr = kgdb_thr_lookup_taddr(addr);

	if (thr == NULL)
		error("invalid thread address");

	kgdb_switch_to_thread(thr);
}

int fbsdcoreops_suppress_target = 1;

void
initialize_kgdb_target(void)
{
	kgdb_trgt_ops.to_magic = OPS_MAGIC;
	kgdb_trgt_ops.to_shortname = "kernel";
	kgdb_trgt_ops.to_longname = "kernel core dump file";
	kgdb_trgt_ops.to_doc = 
    "Use a vmcore file as a target.  Specify the filename of the vmcore file.";
	kgdb_trgt_ops.to_stratum = process_stratum;
	kgdb_trgt_ops.to_has_registers = default_child_has_registers;
	kgdb_trgt_ops.to_has_memory = default_child_has_memory;
	kgdb_trgt_ops.to_has_stack = default_child_has_stack;

	kgdb_trgt_ops.to_open = kgdb_trgt_open;
	kgdb_trgt_ops.to_close = kgdb_trgt_close;
	kgdb_trgt_ops.to_attach = find_default_attach;
	kgdb_trgt_ops.to_detach = kgdb_trgt_detach;
	kgdb_trgt_ops.to_extra_thread_info = kgdb_trgt_extra_thread_info;
	kgdb_trgt_ops.to_fetch_registers = kgdb_trgt_fetch_registers;
	kgdb_trgt_ops.to_files_info = kgdb_trgt_files_info;
	kgdb_trgt_ops.to_find_new_threads = kgdb_trgt_find_new_threads;
	kgdb_trgt_ops.to_pid_to_str = kgdb_trgt_pid_to_str;
	/*
	kgdb_trgt_ops.to_store_registers = NULL;
	*/
	kgdb_trgt_ops.to_thread_alive = kgdb_trgt_thread_alive;
	kgdb_trgt_ops.to_xfer_partial = kgdb_trgt_xfer_partial;
	add_target(&kgdb_trgt_ops);

	add_com ("proc", class_obscure, kgdb_set_proc_cmd,
	   "Set current process context");
	add_com ("tid", class_obscure, kgdb_set_tid_cmd,
	   "Set current thread context");
}
