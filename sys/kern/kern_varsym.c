/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/kern_varsym.c,v 1.1 2003/11/05 23:26:20 dillon Exp $
 */

/*
 * This module implements variable storage and management for variant
 * symlinks.  These variables may also be used for general purposes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ucred.h>
#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/varsym.h>
#include <sys/sysproto.h>

MALLOC_DEFINE(M_VARSYM, "varsym", "variable sets for variant symlinks");

struct varsymset	varsymset_sys;

/*
 * Initialize the variant symlink subsystem
 */
static void
varsym_sysinit(void *dummy)
{
    varsymset_init(&varsymset_sys, NULL);
}
SYSINIT(announce, SI_SUB_INTRINSIC, SI_ORDER_FIRST, varsym_sysinit, NULL);

/*
 * varsym_set() system call
 *
 * (int level, const char *name, const char *data)
 */
int
varsym_set(struct varsym_set_args *uap)
{
    char name[MAXVARSYM_NAME];
    char *buf;
    int error;

    if ((error = copyinstr(uap->name, name, sizeof(name), NULL)) != 0)
	goto done2;
    buf = malloc(MAXVARSYM_DATA, M_TEMP, 0);
    if (uap->data && 
	(error = copyinstr(uap->data, buf, MAXVARSYM_DATA, NULL)) != 0)
    {
	goto done1;
    }
    switch(uap->level) {
    case VARSYM_SYS:
	if ((error = suser(curthread)) != 0)
	    break;
	/* XXX implement per-jail sys */
	/* fall through */
    case VARSYM_USER:
	/* XXX check jail / implement per-jail user */
	/* fall through */
    case VARSYM_PROC:
	if (uap->data) {
	    (void)varsymmake(uap->level, name, NULL);
	    error = varsymmake(uap->level, name, buf);
	} else {
	    error = varsymmake(uap->level, name, NULL);
	}
	break;
    }
done1:
    free(buf, M_TEMP);
done2:
    return(error);
}

/*
 * varsym_get() system call
 *
 * (int mask, const char *wild, char *buf, int bufsize)
 */
int
varsym_get(struct varsym_get_args *uap)
{
    char wild[MAXVARSYM_NAME];
    varsym_t sym;
    int error;
    int dlen;

    if ((error = copyinstr(uap->wild, wild, sizeof(wild), NULL)) != 0)
	goto done;
    sym = varsymfind(uap->mask, wild, strlen(wild));
    if (sym == NULL) {
	error = ENOENT;
	goto done;
    }
    dlen = strlen(sym->vs_data);
    if (dlen < uap->bufsize) {
	copyout(sym->vs_data, uap->buf, dlen + 1);
    } else if (uap->bufsize) {
	copyout("", uap->buf, 1);
    }
    uap->sysmsg_result = dlen + 1;
    varsymdrop(sym);
done:
    return(error);
}

/*
 * Lookup a variant symlink.  XXX use a hash table.
 */
static
struct varsyment *
varsymlookup(struct varsymset *vss, const char *name, int namelen)
{
    struct varsyment *ve;

    TAILQ_FOREACH(ve, &vss->vx_queue, ve_entry) {
	varsym_t var = ve->ve_sym;
	if (var->vs_namelen == namelen && 
	    bcmp(name, var->vs_name, namelen) == 0
	) {
	    return(ve);
	}
    }
    return(NULL);
}

varsym_t
varsymfind(int mask, const char *name, int namelen)
{
    struct proc *p;
    struct varsyment *ve = NULL;
    varsym_t sym;

    if ((mask & (VARSYM_PROC_MASK|VARSYM_USER_MASK)) && (p = curproc) != NULL) {
	if (mask & VARSYM_PROC_MASK)
	    ve = varsymlookup(&p->p_varsymset, name, namelen);
	if (ve == NULL && (mask & VARSYM_USER_MASK))
	    ve = varsymlookup(&p->p_ucred->cr_uidinfo->ui_varsymset, name, namelen);
    }
    if (ve == NULL && (mask & VARSYM_SYS_MASK))
	ve = varsymlookup(&varsymset_sys, name, namelen);
    if (ve) {
	sym = ve->ve_sym;
	++sym->vs_refs;
	return(sym);
    } else {
	return(NULL);
    }
}

int
varsymmake(int level, const char *name, const char *data)
{
    struct varsymset *vss = NULL;
    struct varsyment *ve;
    struct proc *p = curproc;
    varsym_t sym;
    int namelen = strlen(name);
    int datalen;
    int error;

    switch(level) {
    case VARSYM_PROC:
	if (p)
	    vss = &p->p_varsymset;
	break;
    case VARSYM_USER:
	if (p)
	    vss = &p->p_ucred->cr_uidinfo->ui_varsymset;
	break;
    case VARSYM_SYS:
	vss = &varsymset_sys;
	break;
    }
    if (vss == NULL) {
	error = EINVAL;
    } else if (data && vss->vx_setsize >= MAXVARSYM_SET) {
	error = E2BIG;
    } else if (data) {
	datalen = strlen(data);
	ve = malloc(sizeof(struct varsyment), M_VARSYM, M_ZERO);
	sym = malloc(sizeof(struct varsym) + namelen + datalen + 2, M_VARSYM, 0);
	ve->ve_sym = sym;
	sym->vs_refs = 1;
	sym->vs_namelen = namelen;
	sym->vs_name = (char *)(sym + 1);
	sym->vs_data = sym->vs_name + namelen + 1;
	strcpy(sym->vs_name, name);
	strcpy(sym->vs_data, data);
	TAILQ_INSERT_TAIL(&vss->vx_queue, ve, ve_entry);
	vss->vx_setsize += sizeof(struct varsyment) + sizeof(struct varsym) + namelen + datalen + 8;
	error = 0;
    } else {
	if ((ve = varsymlookup(vss, name, namelen)) != NULL) {
	    TAILQ_REMOVE(&vss->vx_queue, ve, ve_entry);
	    vss->vx_setsize -= sizeof(struct varsyment) + sizeof(struct varsym) + namelen + strlen(ve->ve_sym->vs_data) + 8;
	    varsymdrop(ve->ve_sym);
	    free(ve, M_VARSYM);
	    error = 0;
	} else {
	    error = ENOENT;
	}
    }
    return(error);
}

void
varsymdrop(varsym_t sym)
{
    KKASSERT(sym->vs_refs > 0);
    if (--sym->vs_refs == 0) {
	free(sym, M_VARSYM);
    }
}

static void
varsymdup(struct varsymset *vss, struct varsyment *ve)
{
    struct varsyment *nve;

    nve = malloc(sizeof(struct varsyment), M_VARSYM, M_ZERO);
    nve->ve_sym = ve->ve_sym;
    ++nve->ve_sym->vs_refs;
    TAILQ_INSERT_TAIL(&vss->vx_queue, nve, ve_entry);
}

void
varsymset_init(struct varsymset *vss, struct varsymset *copy)
{
    struct varsyment *ve;

    TAILQ_INIT(&vss->vx_queue);
    if (copy) {
	TAILQ_FOREACH(ve, &copy->vx_queue, ve_entry) {
	    varsymdup(vss, ve);
	}
	vss->vx_setsize = copy->vx_setsize;
    }
}

void
varsymset_clean(struct varsymset *vss)
{
    struct varsyment *ve;

    while ((ve = TAILQ_FIRST(&vss->vx_queue)) != NULL) {
	TAILQ_REMOVE(&vss->vx_queue, ve, ve_entry);
	varsymdrop(ve->ve_sym);
	free(ve, M_VARSYM);
    }
    vss->vx_setsize = 0;
}

