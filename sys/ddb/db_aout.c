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
 * $FreeBSD: src/sys/ddb/db_aout.c,v 1.27 1999/08/28 00:41:05 peter Exp $
 */

/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */
/*
 * Symbol table routines for a.out format files.
 */

#if !defined(__ELF__)

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bootinfo.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

#define _AOUT_INCLUDE_
#include <nlist.h>
#include <stab.h>

/*
 * An a.out symbol table as loaded into the kernel debugger:
 *
 * symtab	-> size of symbol entries, in bytes
 * sp		-> first symbol entry
 *		   ...
 * ep		-> last symbol entry + 1
 * strtab	== start of string table
 *		   size of string table in bytes,
 *		   including this word
 *		-> strings
 */

static void	X_db_sym_init (int *symtab, char *esymtab, char *name);
/*
 * Find pointers to the start and end of the symbol entries,
 * given a pointer to the start of the symbol table.
 */
#define	db_get_aout_symtab(symtab, sp, ep) \
	(sp = (struct nlist *)((symtab) + 1), \
	 ep = (struct nlist *)((char *)sp + *(symtab)))

/*
 * Parameters:
 *     symtab:	pointer to start of symbol table
 *     esymtab:	pointer to end of string table, for checking - rounded up to
 *              integer boundary
 */
static void
X_db_sym_init(int *symtab, char *esymtab, char *name)
{
	struct nlist	*sym_start, *sym_end;
	struct nlist	*sp;
	char	*	strtab;
	int	strlen;

	if (*symtab < 4) {
		kprintf ("DDB: no symbols\n");
		return;
	}

	db_get_aout_symtab(symtab, sym_start, sym_end);

	strtab = (char *)sym_end;
	strlen = *(int *)strtab;

	if (strtab + ((strlen + sizeof(int) - 1) & ~(sizeof(int)-1))
	    != esymtab)
	{
	    db_printf("[ %s symbol table not valid ]\n", name);
	    return;
	}

	db_printf("[ preserving %#x bytes of %s symbol table ]\n",
		esymtab - (char *)symtab, name);

	for (sp = sym_start; sp < sym_end; sp++) {
	    int strx;
	    strx = sp->n_un.n_strx;
	    if (strx != 0) {
		if (strx > strlen) {
		    db_printf("Bad string table index (%#x)\n", strx);
		    sp->n_un.n_name = 0;
		    continue;
		}
		sp->n_un.n_name = strtab + strx;
	    }
	}

	db_add_symbol_table((char *)sym_start, (char *)sym_end, name,
			    (char *)symtab);
}

c_db_sym_t
X_db_lookup(db_symtab_t *stab, const char *symstr)
{
	struct nlist *sp, *ep;

	sp = (struct nlist *)stab->start;
	ep = (struct nlist *)stab->end;

	for (; sp < ep; sp++) {
	    if (sp->n_un.n_name == 0)
		continue;
	    if ((sp->n_type & N_STAB) == 0 &&
		sp->n_un.n_name != 0 &&
		db_eqname(sp->n_un.n_name, symstr, '_'))
	    {
		return ((db_sym_t)sp);
	    }
	}
	return ((db_sym_t)0);
}

/*
 * Parameters:
 *     diffp:	in/out
 */
c_db_sym_t
X_db_search_symbol(db_symtab_t *symtab, db_addr_t off, db_strategy_t strategy,
		   db_expr_t *diffp)
{
	unsigned int	diff = *diffp;
	struct nlist	*symp = NULL;
	struct nlist	*sp, *ep;

	sp = (struct nlist *)symtab->start;
	ep = (struct nlist *)symtab->end;

	for (; sp < ep; sp++) {
	    if (sp->n_un.n_name == 0)
		continue;
	    if ((sp->n_type & N_STAB) != 0 || (sp->n_type & N_TYPE) == N_FN)
		continue;
	    if (off >= sp->n_value) {
		if (off - sp->n_value < diff) {
		    diff = off - sp->n_value;
		    symp = sp;
		    if (diff == 0) {
		        if (strategy == DB_STGY_PROC &&
					sp->n_type == (N_TEXT|N_EXT))
		 	    break;
			if (strategy == DB_STGY_ANY &&
					(sp->n_type & N_EXT))
			    break;
		    }
		}
		else if (off - sp->n_value == diff) {
		    if (symp == NULL)
			symp = sp;
		    else if ((symp->n_type & N_EXT) == 0 &&
				(sp->n_type & N_EXT) != 0)
			symp = sp;	/* pick the external symbol */
		}
	    }
	}
	if (symp == NULL) {
	    *diffp = off;
	}
	else {
	    *diffp = diff;
	}
	return ((db_sym_t)symp);
}

/*
 * Return the name and value for a symbol.
 */
void
X_db_symbol_values(db_symtab_t *symtab, c_db_sym_t sym, const char **namep,
		   db_expr_t *valuep)
{
	const struct nlist *sp;

	sp = (const struct nlist *)sym;
	if (namep)
	    *namep = sp->n_un.n_name;
	if (valuep)
	    *valuep = sp->n_value;
}


boolean_t
X_db_line_at_pc(db_symtab_t *symtab, c_db_sym_t cursym, char **filename,
		int *linenum, db_expr_t off)
{
	struct nlist	*sp, *ep;
	unsigned long		sodiff = -1UL, lndiff = -1UL, ln = 0;
	char			*fname = NULL;

	sp = (struct nlist *)symtab->start;
	ep = (struct nlist *)symtab->end;

/*
 * XXX - this used to remove "gcc_compiled.", but that is obsolete.  We
 * now remove unwanted names using symorder.
 */
#define NEWSRC(str)	0

	for (; sp < ep; sp++) {

	    /*
	     * Prevent bogus linenumbers in case module not compiled
	     * with debugging options
	     */
#if 0
	    if (sp->n_value <= off && (off - sp->n_value) <= sodiff &&
		NEWSRC(sp->n_un.n_name)) {
#endif
	    if ((sp->n_type & N_TYPE) == N_FN || NEWSRC(sp->n_un.n_name)) {
		sodiff = lndiff = -1UL;
		ln = 0;
		fname = NULL;
	    }

	    if (sp->n_type == N_SO && *sp->n_un.n_name != '/') {
		if (sp->n_value <= off && (off - sp->n_value) < sodiff) {
			sodiff = off - sp->n_value;
			fname = sp->n_un.n_name;
		}
		continue;
	    }

	    if (sp->n_type != N_SLINE)
		continue;

	    if (sp->n_value > off)
		break;

	    if (off - sp->n_value < lndiff) {
		lndiff = off - sp->n_value;
		ln = sp->n_desc;
	    }
	}

	if (fname != NULL && ln != 0) {
		*filename = fname;
		*linenum = ln;
		return TRUE;
	}

	return (FALSE);
}

boolean_t
X_db_sym_numargs(db_symtab_t *symtab, c_db_sym_t cursym, int *nargp,
		 char **argnamep)
{
	struct nlist	*sp, *ep;
	u_long			addr;
	int			maxnarg = *nargp, nargs = 0;

	if (cursym == NULL)
		return FALSE;

	addr = ((const struct nlist *)cursym)->n_value;
	sp = (struct nlist *)symtab->start;
	ep = (struct nlist *)symtab->end;

	for (; sp < ep; sp++) {
	    if (sp->n_type == N_FUN && sp->n_value == addr) {
		while (++sp < ep && sp->n_type == N_PSYM) {
			if (nargs >= maxnarg)
				break;
			nargs++;
			*argnamep++ = sp->n_un.n_name?sp->n_un.n_name:"???";
			{
			/* XXX - remove trailers */
			char *cp = *(argnamep-1);
			while (*cp != '\0' && *cp != ':') cp++;
			if (*cp == ':') *cp = '\0';
			}
		}
		*nargp = nargs;
		return TRUE;
	    }
	}
	return FALSE;
}

/*
 * Initialization routine for a.out files.
 */
void
kdb_init(void)
{
#ifdef __i386__
	if (bootinfo.bi_esymtab != bootinfo.bi_symtab)
		X_db_sym_init((int *)bootinfo.bi_symtab,
			      (char *)((bootinfo.bi_esymtab + sizeof(int) - 1)
				       & ~(sizeof(int) - 1)),
			      "kernel");
#endif
}

#if 0
/*
 * Read symbol table from file.
 * (should be somewhere else)
 */
#include <boot_ufs/file_io.h>
#include <vm/vm_kern.h>

read_symtab_from_file(struct file *fp, char *symtab_name)
{
	vm_size_t	resid;
	kern_return_t	result;
	vm_offset_t	symoff;
	vm_size_t	symsize;
	vm_offset_t	stroff;
	vm_size_t	strsize;
	vm_size_t	table_size;
	vm_offset_t	symtab;

	if (!get_symtab(fp, &symoff, &symsize)) {
	    boot_printf("[ error %d reading %s file header ]\n",
			result, symtab_name);
	    return;
	}

	stroff = symoff + symsize;
	result = read_file(fp, (vm_offset_t)stroff,
			(vm_offset_t)&strsize, sizeof(strsize), &resid);
	if (result || resid) {
	    boot_printf("[ no valid symbol table present for %s ]\n",
		symtab_name);
		return;
	}

	table_size = sizeof(int) + symsize + strsize;
	table_size = (table_size + sizeof(int)-1) & ~(sizeof(int)-1);

	symtab = kmem_alloc_wired(&kernel_map, table_size);

	*(int *)symtab = symsize;

	result = read_file(fp, symoff,
			symtab + sizeof(int), symsize, &resid);
	if (result || resid) {
	    boot_printf("[ error %d reading %s symbol table ]\n",
			result, symtab_name);
	    return;
	}

	result = read_file(fp, stroff,
			symtab + sizeof(int) + symsize, strsize, &resid);
	if (result || resid) {
	    boot_printf("[ error %d reading %s string table ]\n",
			result, symtab_name);
	    return;
	}

	X_db_sym_init((int *)symtab,
			(char *)(symtab + table_size),
			symtab_name);

}
#endif
#endif
