/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/kern_varsym.c,v 1.6 2005/01/14 02:25:08 joerg Exp $
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
#include <sys/jail.h>
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
 * varsymreplace() - called from namei
 *
 *	Do variant symlink variable substitution
 */
int
varsymreplace(char *cp, int linklen, int maxlen)
{
    int rlen;
    int xlen;
    int nlen;
    int i;
    varsym_t var;

    rlen = linklen;
    while (linklen > 1) {
	if (cp[0] == '$' && cp[1] == '{') {
	    for (i = 2; i < linklen; ++i) {
		if (cp[i] == '}')
		    break;
	    }
	    if (i < linklen && 
		(var = varsymfind(VARSYM_ALL_MASK, cp + 2, i - 2)) != NULL
	    ) {
		xlen = i + 1;			/* bytes to strike */
		nlen = strlen(var->vs_data);	/* bytes to add */
		if (linklen + nlen - xlen >= maxlen) {
		    varsymdrop(var);
		    return(-1);
		}
		KKASSERT(linklen >= xlen);
		if (linklen != xlen)
		    bcopy(cp + xlen, cp + nlen, linklen - xlen);
		bcopy(var->vs_data, cp, nlen);
		linklen += nlen - xlen;	/* new relative length */
		rlen += nlen - xlen;	/* returned total length */
		cp += nlen;		/* adjust past replacement */
		linklen -= nlen;	/* adjust past replacement */
		maxlen -= nlen;		/* adjust past replacement */
	    } else {
		/*
		 * It's ok if i points to the '}', it will simply be
		 * skipped.  i could also have hit linklen.
		 */
		cp += i;
		linklen -= i;
		maxlen -= i;
	    }
	} else {
	    ++cp;
	    --linklen;
	    --maxlen;
	}
    }
    return(rlen);
}

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
    buf = malloc(MAXVARSYM_DATA, M_TEMP, M_WAITOK);
    if (uap->data && 
	(error = copyinstr(uap->data, buf, MAXVARSYM_DATA, NULL)) != 0)
    {
	goto done1;
    }
    switch(uap->level) {
    case VARSYM_SYS:
	if (curthread->td_proc != NULL && curthread->td_proc->p_ucred->cr_prison != NULL)
	    uap->level = VARSYM_PRISON;
    case VARSYM_PRISON:
	if (curthread->td_proc != NULL &&
	    (error = suser_cred(curthread->td_proc->p_ucred, PRISON_ROOT)) != 0)
	    break;
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
 * varsym_list() system call
 *
 * (int level, char *buf, int maxsize, int *marker)
 */
int
varsym_list(struct varsym_list_args *uap)
{
	struct varsymset *vss;
	struct varsyment *ve;
	struct proc *p;
	int i;
	int error;
	int bytes;
	int earlyterm;
	int marker;

	/*
	 * Get the marker from userspace.
	 */
	if ((error = copyin(uap->marker, &marker, sizeof(marker))) != 0)
		goto done;

	/*
	 * Figure out the varsym set.
	 */
	p = curproc;
	vss = NULL;

	switch (uap->level) {
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
	case VARSYM_PRISON:
		if (p != NULL && p->p_ucred->cr_prison != NULL)
			vss = &p->p_ucred->cr_prison->pr_varsymset;
		break;
	}
	if (vss == NULL) {
		error = EINVAL;
		goto done;
	}

	/*
	 * Loop through the variables and dump them to uap->buf
	 */
	i = 0;
	bytes = 0;
	earlyterm = 0;

	TAILQ_FOREACH(ve, &vss->vx_queue, ve_entry) {
		varsym_t sym = ve->ve_sym;
		int namelen = strlen(sym->vs_name);
		int datalen = strlen(sym->vs_data);
		int totlen = namelen + datalen + 2;

		/*
		 * Skip to our index point
		 */
		if (i < marker) {
			++i;
			continue;
		}

		/*
		 * Stop if there is insufficient space in the user buffer.
		 * If we haven't stored anything yet return EOVERFLOW. 
		 * Note that the marker index (i) does not change.
		 */
		if (bytes + totlen > uap->maxsize) {
			if (bytes == 0)
				error = EOVERFLOW;
			earlyterm = 1;
			break;
		}

		error = copyout(sym->vs_name, uap->buf + bytes, namelen + 1);
		if (error == 0) {
			bytes += namelen + 1;
			error = copyout(sym->vs_data, uap->buf + bytes, datalen + 1);
			if (error == 0)
				bytes += datalen + 1;
			else
				bytes -= namelen + 1;	/* revert if error */
		}
		if (error) {
			earlyterm = 1;
			break;
		}
		++i;
	}

	/*
	 * Save the marker back.  If no error occured and earlyterm is clear
	 * the marker is set to -1 indicating that the variable list has been
	 * exhausted.  If no error occured the number of bytes loaded into
	 * the buffer will be returned, otherwise the syscall code returns -1.
	 */
	if (error == 0 && earlyterm == 0)
		marker = -1;
	else
		marker = i;
	if (error == 0)
		error = copyout(&marker, uap->marker, sizeof(marker));
	uap->sysmsg_result = bytes;
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
    struct proc *p = curproc;
    struct varsyment *ve = NULL;
    varsym_t sym;

    if ((mask & (VARSYM_PROC_MASK|VARSYM_USER_MASK)) && p != NULL) {
	if (mask & VARSYM_PROC_MASK)
	    ve = varsymlookup(&p->p_varsymset, name, namelen);
	if (ve == NULL && (mask & VARSYM_USER_MASK))
	    ve = varsymlookup(&p->p_ucred->cr_uidinfo->ui_varsymset, name, namelen);
    }
    if (ve == NULL && (mask & VARSYM_SYS_MASK)) {
	if (p != NULL && p->p_ucred->cr_prison) 
	    ve = varsymlookup(&p->p_ucred->cr_prison->pr_varsymset, name, namelen);
	else
	    ve = varsymlookup(&varsymset_sys, name, namelen);
    }
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
    case VARSYM_PRISON:
	if (p != NULL && p->p_ucred->cr_prison != NULL)
	    vss = &p->p_ucred->cr_prison->pr_varsymset;
	break;
    }
    if (vss == NULL) {
	error = EINVAL;
    } else if (data && vss->vx_setsize >= MAXVARSYM_SET) {
	error = E2BIG;
    } else if (data) {
	datalen = strlen(data);
	ve = malloc(sizeof(struct varsyment), M_VARSYM, M_WAITOK|M_ZERO);
	sym = malloc(sizeof(struct varsym) + namelen + datalen + 2, M_VARSYM, M_WAITOK);
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

    nve = malloc(sizeof(struct varsyment), M_VARSYM, M_WAITOK|M_ZERO);
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

