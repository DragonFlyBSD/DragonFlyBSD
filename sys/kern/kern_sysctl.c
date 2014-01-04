/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
 *
 * Quite extensively rewritten by Poul-Henning Kamp of the FreeBSD
 * project, to make these variables more userfriendly.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_sysctl.c	8.4 (Berkeley) 4/14/94
 * $FreeBSD: src/sys/kern/kern_sysctl.c,v 1.92.2.9 2003/05/01 22:48:09 trhodes Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sysproto.h>
#include <sys/lock.h>
#include <sys/sbuf.h>

#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

static MALLOC_DEFINE(M_SYSCTL, "sysctl", "sysctl internal magic");
static MALLOC_DEFINE(M_SYSCTLOID, "sysctloid", "sysctl dynamic oids");

static struct lock sysctl_lkp;
static struct lock sysctl_ctx_lkp;

static void	sysctl_lock(int type);
static void	sysctl_unlock(void);
static void	sysctl_ctx_lock(int type);
static void	sysctl_ctx_unlock(void);

static int	sysctl_root(SYSCTL_HANDLER_ARGS);
static void	sysctl_register_oid_int(struct sysctl_oid *oipd);
static void	sysctl_unregister_oid_int(struct sysctl_oid *oipd);
static struct sysctl_ctx_entry* sysctl_ctx_entry_find_int
	(struct sysctl_ctx_list *, struct sysctl_oid *oidp);

struct sysctl_oid_list sysctl__children; /* root list */

static struct sysctl_oid *
sysctl_find_oidname(const char *name, struct sysctl_oid_list *list, int lock)
{
	struct sysctl_oid *oidp;

	SLIST_FOREACH(oidp, list, oid_link) {
		if (strcmp(oidp->oid_name, name) == 0) {
			break;
		}
	}
	return (oidp);
}

/*
 * Initialization of the MIB tree.
 *
 * Order by number in each list.
 */

void
sysctl_register_oid(struct sysctl_oid *oidp)
{
	sysctl_lock(LK_EXCLUSIVE);
	sysctl_register_oid_int(oidp);
	sysctl_unlock();
}

static void
sysctl_register_oid_int(struct sysctl_oid *oidp)
{
	struct sysctl_oid_list *parent = oidp->oid_parent;
	struct sysctl_oid *p;
	struct sysctl_oid *q;

	/*
	 * First check if another oid with the same name already
	 * exists in the parent's list.
	 */
	p = sysctl_find_oidname(oidp->oid_name, parent, 0);
	if (p != NULL) {
		if ((p->oid_kind & CTLTYPE) == CTLTYPE_NODE)
			p->oid_refcnt++;
		else
			kprintf("can't re-use a leaf (%s)!\n", p->oid_name);
		return;
	}

	/*
	 * If this oid has a number OID_AUTO, give it a number which
	 * is greater than any current oid.  Make sure it is at least
	 * 256 to leave space for pre-assigned oid numbers.
	 */
	if (oidp->oid_number == OID_AUTO) {
		int newoid = 0x100;	/* minimum AUTO oid */

		/*
		 * Adjust based on highest oid in parent list
		 */
		SLIST_FOREACH(p, parent, oid_link) {
			if (newoid <= p->oid_number)
				newoid = p->oid_number + 1;
		}
		oidp->oid_number = newoid;
	}

	/*
	 * Insert the oid into the parent's list in order.
	 */
	q = NULL;
	SLIST_FOREACH(p, parent, oid_link) {
		if (oidp->oid_number < p->oid_number)
			break;
		q = p;
	}
	if (q)
		SLIST_INSERT_AFTER(q, oidp, oid_link);
	else
		SLIST_INSERT_HEAD(parent, oidp, oid_link);
}

void
sysctl_unregister_oid(struct sysctl_oid *oidp)
{
	sysctl_lock(LK_EXCLUSIVE);
	sysctl_unregister_oid_int(oidp);
	sysctl_unlock();
}

static void
sysctl_unregister_oid_int(struct sysctl_oid *oidp)
{
	struct sysctl_oid *p;

	if (oidp->oid_number == OID_AUTO)
		panic("Trying to unregister OID_AUTO entry: %p", oidp);

	SLIST_FOREACH(p, oidp->oid_parent, oid_link) {
		if (p != oidp)
			continue;
		SLIST_REMOVE(oidp->oid_parent, oidp, sysctl_oid, oid_link);
		return;
	}

	/*
	 * This can happen when a module fails to register and is
	 * being unloaded afterwards.  It should not be a panic()
	 * for normal use.
	 */
	kprintf("%s: failed to unregister sysctl\n", __func__);
}

/* Initialize a new context to keep track of dynamically added sysctls. */
int
sysctl_ctx_init(struct sysctl_ctx_list *c)
{
	if (c == NULL)
		return(EINVAL);
	TAILQ_INIT(c);
	return(0);
}

/* Free the context, and destroy all dynamic oids registered in this context */
int
sysctl_ctx_free(struct sysctl_ctx_list *clist)
{
	struct sysctl_ctx_entry *e, *e1;
	int error;

	error = 0;
	sysctl_ctx_lock(LK_EXCLUSIVE);
	/*
	 * First perform a "dry run" to check if it's ok to remove oids.
	 * XXX FIXME
	 * XXX This algorithm is a hack. But I don't know any
	 * XXX better solution for now...
	 */
	TAILQ_FOREACH(e, clist, link) {
		error = sysctl_remove_oid(e->entry, 0, 0);
		if (error)
			break;
	}
	/*
	 * Restore deregistered entries, either from the end,
	 * or from the place where error occured.
	 * e contains the entry that was not unregistered
	 */
	if (error)
		e1 = TAILQ_PREV(e, sysctl_ctx_list, link);
	else
		e1 = TAILQ_LAST(clist, sysctl_ctx_list);
	while (e1 != NULL) {
		sysctl_register_oid(e1->entry);
		e1 = TAILQ_PREV(e1, sysctl_ctx_list, link);
	}
	if (error) {
		sysctl_ctx_unlock();
		return(EBUSY);
	}
	/* Now really delete the entries */
	e = TAILQ_FIRST(clist);
	while (e != NULL) {
		e1 = TAILQ_NEXT(e, link);
		error = sysctl_remove_oid(e->entry, 1, 0);
		if (error)
			panic("sysctl_remove_oid: corrupt tree, entry: %s",
			    e->entry->oid_name);
		kfree(e, M_SYSCTLOID);
		e = e1;
	}
	sysctl_ctx_unlock();
	return (error);
}

/* Add an entry to the context */
struct sysctl_ctx_entry *
sysctl_ctx_entry_add(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	if (clist == NULL || oidp == NULL)
		return(NULL);
	e = kmalloc(sizeof(struct sysctl_ctx_entry), M_SYSCTLOID, M_WAITOK);
	e->entry = oidp;
	sysctl_ctx_lock(LK_EXCLUSIVE);
	TAILQ_INSERT_HEAD(clist, e, link);
	sysctl_ctx_unlock();
	return (e);
}

/* Find an entry in the context */
struct sysctl_ctx_entry *
sysctl_ctx_entry_find(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	if (clist == NULL || oidp == NULL)
		return(NULL);

	sysctl_ctx_lock(LK_SHARED);
	e = sysctl_ctx_entry_find_int(clist, oidp);
	sysctl_ctx_unlock();

	return(e);
}

struct sysctl_ctx_entry *
sysctl_ctx_entry_find_int(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	KKASSERT(clist != NULL && oidp != NULL);

	for (e = TAILQ_FIRST(clist); e != NULL; e = TAILQ_NEXT(e, link)) {
		if(e->entry == oidp)
			break;
	}

	return (e);
}

/*
 * Delete an entry from the context.
 * NOTE: this function doesn't free oidp! You have to remove it
 * with sysctl_remove_oid().
 */
int
sysctl_ctx_entry_del(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	if (clist == NULL || oidp == NULL)
		return (EINVAL);

	sysctl_ctx_lock(LK_EXCLUSIVE);
	e = sysctl_ctx_entry_find_int(clist, oidp);
	if (e == NULL) {
		sysctl_ctx_unlock();
		return (ENOENT);
	}
	TAILQ_REMOVE(clist, e, link);
	kfree(e, M_SYSCTLOID);
	sysctl_ctx_unlock();

	return(0);
}

/*
 * Remove dynamically created sysctl trees.
 * oidp - top of the tree to be removed
 * del - if 0 - just deregister, otherwise free up entries as well
 * recurse - if != 0 traverse the subtree to be deleted
 */
int
sysctl_remove_oid(struct sysctl_oid *oidp, int del, int recurse)
{
	struct sysctl_oid *p;
	int error;

	if (oidp == NULL)
		return(EINVAL);
	if ((oidp->oid_kind & CTLFLAG_DYN) == 0) {
		kprintf("can't remove non-dynamic nodes!\n");
		return (EINVAL);
	}
	sysctl_lock(LK_EXCLUSIVE | LK_CANRECURSE);
	/*
	 * WARNING: normal method to do this should be through
	 * sysctl_ctx_free(). Use recursing as the last resort
	 * method to purge your sysctl tree of leftovers...
	 * However, if some other code still references these nodes,
	 * it will panic.
	 */
	if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		if (oidp->oid_refcnt == 1) {
			SLIST_FOREACH(p, SYSCTL_CHILDREN(oidp), oid_link) {
				if (!recurse) {
					sysctl_unlock();
					return(ENOTEMPTY);
				}
				error = sysctl_remove_oid(p, del, recurse);
				if (error) {
					sysctl_unlock();
					return(error);
				}
			}
			if (del)
				kfree(SYSCTL_CHILDREN(oidp), M_SYSCTLOID);
		}
	}
	if (oidp->oid_refcnt > 1 ) {
		oidp->oid_refcnt--;
	} else {
		if (oidp->oid_refcnt == 0) {
			kprintf("Warning: bad oid_refcnt=%u (%s)!\n",
			       oidp->oid_refcnt, oidp->oid_name);
			sysctl_unlock();
			return(EINVAL);
		}
		sysctl_unregister_oid_int(oidp);
		if (del) {
			if (oidp->oid_descr)
				kfree(__DECONST(char *,oidp->oid_descr),
				     M_SYSCTLOID);
			kfree(__DECONST(char *, oidp->oid_name), M_SYSCTLOID);
			kfree(oidp, M_SYSCTLOID);
		}
	}
	sysctl_unlock();
	return(0);
}

/*
 * Create new sysctls at run time.
 * clist may point to a valid context initialized with sysctl_ctx_init().
 */
struct sysctl_oid *
sysctl_add_oid(struct sysctl_ctx_list *clist, struct sysctl_oid_list *parent,
	int number, const char *name, int kind, void *arg1, int arg2,
	int (*handler)(SYSCTL_HANDLER_ARGS), const char *fmt, const char *descr)
{
	struct sysctl_oid *oidp;
	ssize_t len;
	char *newname;

	/* You have to hook up somewhere.. */
	if (parent == NULL)
		return(NULL);
	sysctl_lock(LK_EXCLUSIVE);
	/* Check if the node already exists, otherwise create it */
	oidp = sysctl_find_oidname(name, parent, 0);
	if (oidp != NULL) {
		if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
			oidp->oid_refcnt++;
			/* Update the context */
			if (clist != NULL)
				sysctl_ctx_entry_add(clist, oidp);
			sysctl_unlock();
			return (oidp);
		} else {
			kprintf("can't re-use a leaf (%s)!\n", name);
			sysctl_unlock();
			return (NULL);
		}
	}
	oidp = kmalloc(sizeof(struct sysctl_oid), M_SYSCTLOID, M_WAITOK | M_ZERO);
	oidp->oid_parent = parent;
	SLIST_NEXT(oidp, oid_link) = NULL;
	oidp->oid_number = number;
	oidp->oid_refcnt = 1;
	len = strlen(name);
	newname = kmalloc(len + 1, M_SYSCTLOID, M_WAITOK);
	bcopy(name, newname, len + 1);
	newname[len] = '\0';
	oidp->oid_name = newname;
	oidp->oid_handler = handler;
	oidp->oid_kind = CTLFLAG_DYN | kind;
	if ((kind & CTLTYPE) == CTLTYPE_NODE) {
		struct sysctl_oid_list *children;

		/* Allocate space for children */
		children = kmalloc(sizeof(*children), M_SYSCTLOID, M_WAITOK);
		SYSCTL_SET_CHILDREN(oidp, children);
		SLIST_INIT(children);
	} else {
		oidp->oid_arg1 = arg1;
		oidp->oid_arg2 = arg2;
	}
	oidp->oid_fmt = fmt;
	if (descr) {
		int len = strlen(descr) + 1;
		oidp->oid_descr = kmalloc(len, M_SYSCTLOID, M_WAITOK);
		strcpy((char *)(uintptr_t)(const void *)oidp->oid_descr, descr);
	};
	/* Update the context, if used */
	if (clist != NULL)
		sysctl_ctx_entry_add(clist, oidp);
	/* Register this oid */
	sysctl_register_oid_int(oidp);
	sysctl_unlock();
	return (oidp);
}

/*
 * Register the kernel's oids on startup.
 */
SET_DECLARE(sysctl_set, struct sysctl_oid);

static void
sysctl_register_all(void *arg)
{
	struct sysctl_oid **oidp;

	lockinit(&sysctl_lkp, "sysctl", 0, 0);
	lockinit(&sysctl_ctx_lkp, "sysctl ctx", 0, 0);
	SET_FOREACH(oidp, sysctl_set)
		sysctl_register_oid_int(*oidp);
}

SYSINIT(sysctl, SI_BOOT1_POST, SI_ORDER_ANY, sysctl_register_all, 0);

/*
 * "Staff-functions"
 *
 * These functions implement a presently undocumented interface 
 * used by the sysctl program to walk the tree, and get the type
 * so it can print the value.
 * This interface is under work and consideration, and should probably
 * be killed with a big axe by the first person who can find the time.
 * (be aware though, that the proper interface isn't as obvious as it
 * may seem, there are various conflicting requirements.
 *
 * {0,0}	kprintf the entire MIB-tree.
 * {0,1,...}	return the name of the "..." OID.
 * {0,2,...}	return the next OID.
 * {0,3}	return the OID of the name in "new"
 * {0,4,...}	return the kind & format info for the "..." OID.
 */

static void
sysctl_sysctl_debug_dump_node(struct sysctl_oid_list *l, int i)
{
	int k;
	struct sysctl_oid *oidp;

	sysctl_lock(LK_SHARED);
	SLIST_FOREACH(oidp, l, oid_link) {

		for (k=0; k<i; k++)
			kprintf(" ");

		kprintf("%d %s ", oidp->oid_number, oidp->oid_name);

		kprintf("%c%c",
			oidp->oid_kind & CTLFLAG_RD ? 'R':' ',
			oidp->oid_kind & CTLFLAG_WR ? 'W':' ');

		if (oidp->oid_handler)
			kprintf(" *Handler");

		switch (oidp->oid_kind & CTLTYPE) {
			case CTLTYPE_NODE:
				kprintf(" Node\n");
				if (!oidp->oid_handler) {
					sysctl_sysctl_debug_dump_node(
						oidp->oid_arg1, i+2);
				}
				break;
			case CTLTYPE_INT:    kprintf(" Int\n"); break;
			case CTLTYPE_STRING: kprintf(" String\n"); break;
			case CTLTYPE_QUAD:   kprintf(" Quad\n"); break;
			case CTLTYPE_OPAQUE: kprintf(" Opaque/struct\n"); break;
			default:	     kprintf("\n");
		}

	}
	sysctl_unlock();
}

static int
sysctl_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = priv_check(req->td, PRIV_SYSCTL_DEBUG);
	if (error)
		return error;
	sysctl_sysctl_debug_dump_node(&sysctl__children, 0);
	return ENOENT;
}

SYSCTL_PROC(_sysctl, 0, debug, CTLTYPE_STRING|CTLFLAG_RD,
	0, 0, sysctl_sysctl_debug, "-", "");

static int
sysctl_sysctl_name(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *) arg1;
	u_int namelen = arg2;
	int error = 0;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *lsp = &sysctl__children, *lsp2;
	char buf[16];

	sysctl_lock(LK_SHARED);
	while (namelen) {
		if (!lsp) {
			ksnprintf(buf, sizeof(buf), "%d", *name);
			if (req->oldidx)
				error = SYSCTL_OUT(req, ".", 1);
			if (!error)
				error = SYSCTL_OUT(req, buf, strlen(buf));
			if (error) {
				sysctl_unlock();
				return (error);
			}
			namelen--;
			name++;
			continue;
		}
		lsp2 = NULL;
		SLIST_FOREACH(oid, lsp, oid_link) {
			if (oid->oid_number != *name)
				continue;

			if (req->oldidx)
				error = SYSCTL_OUT(req, ".", 1);
			if (!error)
				error = SYSCTL_OUT(req, oid->oid_name,
					strlen(oid->oid_name));
			if (error) {
				sysctl_unlock();
				return (error);
			}

			namelen--;
			name++;

			if ((oid->oid_kind & CTLTYPE) != CTLTYPE_NODE) 
				break;

			if (oid->oid_handler)
				break;

			lsp2 = (struct sysctl_oid_list *)oid->oid_arg1;
			break;
		}
		lsp = lsp2;
	}
	sysctl_unlock();
	return (SYSCTL_OUT(req, "", 1));
}

SYSCTL_NODE(_sysctl, 1, name, CTLFLAG_RD, sysctl_sysctl_name, "");

static int
sysctl_sysctl_next_ls(struct sysctl_oid_list *lsp, int *name, u_int namelen, 
	int *next, int *len, int level, struct sysctl_oid **oidpp)
{
	struct sysctl_oid *oidp;

	*len = level;
	sysctl_lock(LK_SHARED);
	SLIST_FOREACH(oidp, lsp, oid_link) {
		*next = oidp->oid_number;
		*oidpp = oidp;

		if (!namelen) {
			if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE) {
				sysctl_unlock();
				return 0;
			}
			if (oidp->oid_handler) {
				/* We really should call the handler here...*/
				sysctl_unlock();
				return 0;
			}
			lsp = (struct sysctl_oid_list *)oidp->oid_arg1;
			if (!sysctl_sysctl_next_ls(lsp, 0, 0, next+1, 
				len, level+1, oidpp)) {
				sysctl_unlock();
				return 0;
			}
			goto emptynode;
		}

		if (oidp->oid_number < *name)
			continue;

		if (oidp->oid_number > *name) {
			if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE) {
				sysctl_unlock();
				return 0;
			}
			if (oidp->oid_handler) {
				sysctl_unlock();
				return 0;
			}
			lsp = (struct sysctl_oid_list *)oidp->oid_arg1;
			if (!sysctl_sysctl_next_ls(lsp, name+1, namelen-1, 
				next+1, len, level+1, oidpp)) {
				sysctl_unlock();
				return (0);
			}
			goto next;
		}
		if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
			continue;

		if (oidp->oid_handler)
			continue;

		lsp = (struct sysctl_oid_list *)oidp->oid_arg1;
		if (!sysctl_sysctl_next_ls(lsp, name+1, namelen-1, next+1, 
			len, level+1, oidpp)) {
			sysctl_unlock();
			return (0);
		}
	next:
		namelen = 1;
		*len = level;
	emptynode:
		*len = level;
	}
	sysctl_unlock();
	return 1;
}

static int
sysctl_sysctl_next(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *) arg1;
	u_int namelen = arg2;
	int i, j, error;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *lsp = &sysctl__children;
	int newoid[CTL_MAXNAME];

	i = sysctl_sysctl_next_ls(lsp, name, namelen, newoid, &j, 1, &oid);
	if (i)
		return ENOENT;
	error = SYSCTL_OUT(req, newoid, j * sizeof (int));
	return (error);
}

SYSCTL_NODE(_sysctl, 2, next, CTLFLAG_RD, sysctl_sysctl_next, "");

static int
name2oid (char *name, int *oid, int *len, struct sysctl_oid **oidpp)
{
	int i;
	struct sysctl_oid *oidp;
	struct sysctl_oid_list *lsp = &sysctl__children;
	char *p;

	if (!*name)
		return ENOENT;

	p = name + strlen(name) - 1 ;
	if (*p == '.')
		*p = '\0';

	*len = 0;

	for (p = name; *p && *p != '.'; p++) 
		;
	i = *p;
	if (i == '.')
		*p = '\0';

	sysctl_lock(LK_SHARED);
	oidp = SLIST_FIRST(lsp);

	while (oidp && *len < CTL_MAXNAME) {
		if (strcmp(name, oidp->oid_name)) {
			oidp = SLIST_NEXT(oidp, oid_link);
			continue;
		}
		*oid++ = oidp->oid_number;
		(*len)++;

		if (!i) {
			if (oidpp)
				*oidpp = oidp;
			sysctl_unlock();
			return (0);
		}

		if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
			break;

		if (oidp->oid_handler)
			break;

		lsp = (struct sysctl_oid_list *)oidp->oid_arg1;
		oidp = SLIST_FIRST(lsp);
		name = p+1;
		for (p = name; *p && *p != '.'; p++) 
				;
		i = *p;
		if (i == '.')
			*p = '\0';
	}
	sysctl_unlock();
	return ENOENT;
}

static int
sysctl_sysctl_name2oid(SYSCTL_HANDLER_ARGS)
{
	char *p;
	int error, oid[CTL_MAXNAME], len;
	struct sysctl_oid *op = NULL;

	if (!req->newlen) 
		return ENOENT;
	if (req->newlen >= MAXPATHLEN)	/* XXX arbitrary, undocumented */
		return (ENAMETOOLONG);

	p = kmalloc(req->newlen+1, M_SYSCTL, M_WAITOK);

	error = SYSCTL_IN(req, p, req->newlen);
	if (error) {
		kfree(p, M_SYSCTL);
		return (error);
	}

	p [req->newlen] = '\0';

	error = name2oid(p, oid, &len, &op);

	kfree(p, M_SYSCTL);

	if (error)
		return (error);

	error = SYSCTL_OUT(req, oid, len * sizeof *oid);
	return (error);
}

SYSCTL_PROC(_sysctl, 3, name2oid, CTLFLAG_RW|CTLFLAG_ANYBODY, 0, 0, 
	sysctl_sysctl_name2oid, "I", "");

static int
sysctl_sysctl_oidfmt(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	int error;

	error = sysctl_find_oid(arg1, arg2, &oid, NULL, req);
	if (error)
		return (error);

	if (!oid->oid_fmt)
		return (ENOENT);
	error = SYSCTL_OUT(req, &oid->oid_kind, sizeof(oid->oid_kind));
	if (error)
		return (error);
	error = SYSCTL_OUT(req, oid->oid_fmt, strlen(oid->oid_fmt) + 1);
	return (error);
}


SYSCTL_NODE(_sysctl, 4, oidfmt, CTLFLAG_RD, sysctl_sysctl_oidfmt, "");

static int
sysctl_sysctl_oiddescr(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	int error;

	error = sysctl_find_oid(arg1, arg2, &oid, NULL, req);
	if (error)
		return (error);
	
	if (!oid->oid_descr)
		return (ENOENT);
	error = SYSCTL_OUT(req, oid->oid_descr, strlen(oid->oid_descr) + 1);
	return (error);
}

SYSCTL_NODE(_sysctl, 5, oiddescr, CTLFLAG_RD, sysctl_sysctl_oiddescr, "");

/*
 * Default "handler" functions.
 */

/*
 * Handle an int, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_int(SYSCTL_HANDLER_ARGS)
{
	int error = 0;

	if (arg1)
		error = SYSCTL_OUT(req, arg1, sizeof(int));
	else
		error = SYSCTL_OUT(req, &arg2, sizeof(int));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(int));
	return (error);
}

/*
 * Handle a long, signed or unsigned.  arg1 points to it.
 */

int
sysctl_handle_long(SYSCTL_HANDLER_ARGS)
{
	int error = 0;

	if (!arg1)
		return (EINVAL);
	error = SYSCTL_OUT(req, arg1, sizeof(long));

	if (error || !req->newptr)
		return (error);

	error = SYSCTL_IN(req, arg1, sizeof(long));
	return (error);
}

/*
 * Handle a quad, signed or unsigned.  arg1 points to it.
 */

int
sysctl_handle_quad(SYSCTL_HANDLER_ARGS)
{
	int error = 0;

	if (!arg1)
		return (EINVAL);
	error = SYSCTL_OUT(req, arg1, sizeof(quad_t));

	if (error || !req->newptr)
		return (error);

	error = SYSCTL_IN(req, arg1, sizeof(quad_t));
	return (error);
}

/*
 * Handle our generic '\0' terminated 'C' string.
 * Two cases:
 * 	a variable string:  point arg1 at it, arg2 is max length.
 * 	a constant string:  point arg1 at it, arg2 is zero.
 */

int
sysctl_handle_string(SYSCTL_HANDLER_ARGS)
{
	int error=0;

	error = SYSCTL_OUT(req, arg1, strlen((char *)arg1)+1);

	if (error || !req->newptr)
		return (error);

	if ((req->newlen - req->newidx) >= arg2) {
		error = EINVAL;
	} else {
		arg2 = (req->newlen - req->newidx);
		error = SYSCTL_IN(req, arg1, arg2);
		((char *)arg1)[arg2] = '\0';
	}

	return (error);
}

/*
 * Handle any kind of opaque data.
 * arg1 points to it, arg2 is the size.
 */

int
sysctl_handle_opaque(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = SYSCTL_OUT(req, arg1, arg2);

	if (error || !req->newptr)
		return (error);

	error = SYSCTL_IN(req, arg1, arg2);

	return (error);
}

/*
 * Transfer functions to/from kernel space.
 * XXX: rather untested at this point
 */
static int
sysctl_old_kernel(struct sysctl_req *req, const void *p, size_t l)
{
	size_t i = 0;

	if (req->oldptr) {
		i = l;
		if (i > req->oldlen - req->oldidx)
			i = req->oldlen - req->oldidx;
		if (i > 0)
			bcopy(p, (char *)req->oldptr + req->oldidx, i);
	}
	req->oldidx += l;
	if (req->oldptr && i != l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_kernel(struct sysctl_req *req, void *p, size_t l)
{

	if (!req->newptr)
		return 0;
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	bcopy((char *)req->newptr + req->newidx, p, l);
	req->newidx += l;
	return (0);
}

int
kernel_sysctl(int *name, u_int namelen, void *old, size_t *oldlenp, void *new, size_t newlen, size_t *retval)
{
	int error = 0;
	struct sysctl_req req;

	bzero(&req, sizeof req);

	req.td = curthread;

	if (oldlenp) {
		req.oldlen = *oldlenp;
	}

	if (old) {
		req.oldptr = old;
	}

	if (new != NULL) {
		req.newlen = newlen;
		req.newptr = new;
	}

	req.oldfunc = sysctl_old_kernel;
	req.newfunc = sysctl_new_kernel;
#if 0
	req.lock = 1;
#endif

	sysctl_lock(LK_SHARED);

	error = sysctl_root(0, name, namelen, &req);

#if 0
	if (req.lock == 2)
		vsunlock(req.oldptr, req.oldlen);
#endif

	sysctl_unlock();

	if (error && error != ENOMEM)
		return (error);

	if (retval) {
		if (req.oldptr && req.oldidx > req.oldlen)
			*retval = req.oldlen;
		else
			*retval = req.oldidx;
	}
	return (error);
}

int
kernel_sysctlbyname(char *name, void *old, size_t *oldlenp,
    void *new, size_t newlen, size_t *retval)
{
        int oid[CTL_MAXNAME];
        size_t oidlen, plen;
	int error;

	oid[0] = 0;		/* sysctl internal magic */
	oid[1] = 3;		/* name2oid */
	oidlen = sizeof(oid);

	error = kernel_sysctl(oid, 2, oid, &oidlen, name, strlen(name), &plen);
	if (error)
		return (error);

	error = kernel_sysctl(oid, plen / sizeof(int), old, oldlenp,
	    new, newlen, retval);
	return (error);
}

/*
 * Transfer function to/from user space.
 */
static int
sysctl_old_user(struct sysctl_req *req, const void *p, size_t l)
{
	int error = 0;
	size_t i = 0;

#if 0
	if (req->lock == 1 && req->oldptr) {
		vslock(req->oldptr, req->oldlen);
		req->lock = 2;
	}
#endif
	if (req->oldptr) {
		i = l;
		if (i > req->oldlen - req->oldidx)
			i = req->oldlen - req->oldidx;
		if (i > 0)
			error = copyout(p, (char *)req->oldptr + req->oldidx,
					i);
	}
	req->oldidx += l;
	if (error)
		return (error);
	if (req->oldptr && i < l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_user(struct sysctl_req *req, void *p, size_t l)
{
	int error;

	if (!req->newptr)
		return 0;
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	error = copyin((char *)req->newptr + req->newidx, p, l);
	req->newidx += l;
	return (error);
}

int
sysctl_find_oid(int *name, u_int namelen, struct sysctl_oid **noid,
    int *nindx, struct sysctl_req *req)
{
	struct sysctl_oid *oid;
	int indx;

	sysctl_lock(LK_SHARED);
	oid = SLIST_FIRST(&sysctl__children);
	indx = 0;
	while (oid && indx < CTL_MAXNAME) {
		if (oid->oid_number == name[indx]) {
			indx++;
			if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
				if (oid->oid_handler != NULL ||
				    indx == namelen) {
					*noid = oid;
					if (nindx != NULL)
						*nindx = indx;
					sysctl_unlock();
					return (0);
				}
				oid = SLIST_FIRST(
				    (struct sysctl_oid_list *)oid->oid_arg1);
			} else if (indx == namelen) {
				*noid = oid;
				if (nindx != NULL)
					*nindx = indx;
				sysctl_unlock();
				return (0);
			} else {
				sysctl_unlock();
				return (ENOTDIR);
			}
		} else {
			oid = SLIST_NEXT(oid, oid_link);
		}
	}
	sysctl_unlock();
	return (ENOENT);
}

/*
 * Traverse our tree, and find the right node, execute whatever it points
 * to, and return the resulting error code.
 */

int
sysctl_root(SYSCTL_HANDLER_ARGS)
{
	struct thread *td = req->td;
	struct proc *p = td ? td->td_proc : NULL;
	struct sysctl_oid *oid;
	int error, indx;

	error = sysctl_find_oid(arg1, arg2, &oid, &indx, req);
	if (error)
		return (error);

	if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		/*
		 * You can't call a sysctl when it's a node, but has
		 * no handler.  Inform the user that it's a node.
		 * The indx may or may not be the same as namelen.
		 */
		if (oid->oid_handler == NULL)
			return (EISDIR);
	}

	/* If writing isn't allowed */
	if (req->newptr && (!(oid->oid_kind & CTLFLAG_WR) ||
	    ((oid->oid_kind & CTLFLAG_SECURE) && securelevel > 0)))
		return (EPERM);

	/* Most likely only root can write */
	if (!(oid->oid_kind & CTLFLAG_ANYBODY) && req->newptr && p &&
	    (error = priv_check_cred(td->td_ucred,
	     (oid->oid_kind & CTLFLAG_PRISON) ? PRIV_SYSCTL_WRITEJAIL :
	                                        PRIV_SYSCTL_WRITE, 0)))
		return (error);

	if (!oid->oid_handler)
		return EINVAL;

	if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE)
		error = oid->oid_handler(oid, (int *)arg1 + indx, arg2 - indx,
		    req);
	else
		error = oid->oid_handler(oid, oid->oid_arg1, oid->oid_arg2,
		    req);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys___sysctl(struct sysctl_args *uap)
{
	int error, i, name[CTL_MAXNAME];
	size_t j;

	if (uap->namelen > CTL_MAXNAME || uap->namelen < 2)
		return (EINVAL);

	error = copyin(uap->name, &name, uap->namelen * sizeof(int));
 	if (error)
		return (error);

	error = userland_sysctl(name, uap->namelen,
		uap->old, uap->oldlenp, 0,
		uap->new, uap->newlen, &j);
	if (error && error != ENOMEM)
		return (error);
	if (uap->oldlenp) {
		i = copyout(&j, uap->oldlenp, sizeof(j));
		if (i)
			return (i);
	}
	return (error);
}

/*
 * This is used from various compatibility syscalls too.  That's why name
 * must be in kernel space.
 */
int
userland_sysctl(int *name, u_int namelen, void *old, size_t *oldlenp, int inkernel, void *new, size_t newlen, size_t *retval)
{
	int error = 0;
	struct sysctl_req req, req2;

	bzero(&req, sizeof req);

	if (oldlenp) {
		if (inkernel) {
			req.oldlen = *oldlenp;
		} else {
			error = copyin(oldlenp, &req.oldlen, sizeof(*oldlenp));
			if (error)
				return (error);
		}
	}

	if (old) {
		if (!useracc(old, req.oldlen, VM_PROT_WRITE))
			return (EFAULT);
		req.oldptr= old;
	}

	if (new != NULL) {
		if (!useracc(new, newlen, VM_PROT_READ))
			return (EFAULT);
		req.newlen = newlen;
		req.newptr = new;
	}

	req.oldfunc = sysctl_old_user;
	req.newfunc = sysctl_new_user;
#if 0
	req.lock = 1;
#endif
	req.td = curthread;

	sysctl_lock(LK_SHARED);

	do {
	    req2 = req;
	    error = sysctl_root(0, name, namelen, &req2);
	} while (error == EAGAIN);

	req = req2;
#if 0
	if (req.lock == 2)
		vsunlock(req.oldptr, req.oldlen);
#endif

	sysctl_unlock();

	if (error && error != ENOMEM)
		return (error);

	if (retval) {
		if (req.oldptr && req.oldidx > req.oldlen)
			*retval = req.oldlen;
		else
			*retval = req.oldidx;
	}
	return (error);
}

static void
sysctl_lock(int flag)
{
	lockmgr(&sysctl_lkp, flag);
}

static void
sysctl_unlock(void)
{
	lockmgr(&sysctl_lkp, LK_RELEASE);
}

static void
sysctl_ctx_lock(int flag)
{
	lockmgr(&sysctl_ctx_lkp, flag);
}

static void
sysctl_ctx_unlock(void)
{
	lockmgr(&sysctl_ctx_lkp, LK_RELEASE);
}

int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;
	return (0);
}

/*
 * Drain into a sysctl struct.  The user buffer should be wired if a page
 * fault would cause issue.
 */
static int
sbuf_sysctl_drain(void *arg, const char *data, int len)
{
	struct sysctl_req *req = arg;
	int error;

	error = SYSCTL_OUT(req, data, len);
	KASSERT(error >= 0, ("Got unexpected negative value %d", error));
	return (error == 0 ? len : -error);
}

struct sbuf *
sbuf_new_for_sysctl(struct sbuf *s, char *buf, int length,
    struct sysctl_req *req)
{

	s = sbuf_new(s, buf, length, SBUF_FIXEDLEN);
	sbuf_set_drain(s, sbuf_sysctl_drain, req);
	return (s);
}
