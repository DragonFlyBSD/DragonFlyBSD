/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)if.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/net/if.c,v 1.185 2004/03/13 02:35:03 brooks Exp $
 * $DragonFly: src/sys/net/if_clone.c,v 1.1 2008/01/11 11:59:40 sephe Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_clone.h>

static LIST_HEAD(, if_clone) if_cloners = LIST_HEAD_INITIALIZER(if_cloners);
static int		if_cloners_count;

MALLOC_DEFINE(M_CLONE, "clone", "interface cloning framework");

static struct if_clone	*if_clone_lookup(const char *, int *);

/*
 * Create a clone network interface.
 */
int
if_clone_create(char *name, int len, caddr_t params)
{
	struct if_clone *ifc;
	char *dp;
	int wildcard, bytoff, bitoff;
	int unit;
	int err;

	ifc = if_clone_lookup(name, &unit);
	if (ifc == NULL)
		return (EINVAL);

	ifnet_lock();
	if (ifunit(name) != NULL) {
		ifnet_unlock();
		return (EEXIST);
	}
	ifnet_unlock();

	bytoff = bitoff = 0;
	wildcard = (unit < 0);
	/*
	 * Find a free unit if none was given.
	 */
	if (wildcard) {
		while (bytoff < ifc->ifc_bmlen &&
		    ifc->ifc_units[bytoff] == 0xff)
			bytoff++;
		if (bytoff >= ifc->ifc_bmlen)
			return (ENOSPC);
		while ((ifc->ifc_units[bytoff] & (1 << bitoff)) != 0)
			bitoff++;
		unit = (bytoff << 3) + bitoff;
	}

	if (unit > ifc->ifc_maxunit)
		return (ENXIO);

	err = (*ifc->ifc_create)(ifc, unit, params);
	if (err != 0)
		return (err);

	if (!wildcard) {
		bytoff = unit >> 3;
		bitoff = unit - (bytoff << 3);
	}

	/*
	 * Allocate the unit in the bitmap.
	 */
	KASSERT((ifc->ifc_units[bytoff] & (1 << bitoff)) == 0,
	    ("%s: bit is already set", __func__));
	ifc->ifc_units[bytoff] |= (1 << bitoff);

	/* In the wildcard case, we need to update the name. */
	if (wildcard) {
		for (dp = name; *dp != '\0'; dp++);
		if (ksnprintf(dp, len - (dp-name), "%d", unit) >
		    len - (dp-name) - 1) {
			/*
			 * This can only be a programmer error and
			 * there's no straightforward way to recover if
			 * it happens.
			 */
			panic("if_clone_create(): interface name too long");
		}

	}

	EVENTHANDLER_INVOKE(if_clone_event, ifc);

	return (0);
}

/*
 * Destroy a clone network interface.
 */
int
if_clone_destroy(const char *name)
{
	struct if_clone *ifc;
	struct ifnet *ifp;
	int bytoff, bitoff;
	int unit, error;

	ifc = if_clone_lookup(name, &unit);
	if (ifc == NULL)
		return (EINVAL);

	if (unit < ifc->ifc_minifs)
		return (EINVAL);

	ifnet_lock();

	ifp = ifunit(name);
	if (ifp == NULL) {
		ifnet_unlock();
		return (ENXIO);
	}

	if (ifc->ifc_destroy == NULL) {
		ifnet_unlock();
		return (EOPNOTSUPP);
	}

	error = ifc->ifc_destroy(ifp);
	if (error) {
		ifnet_unlock();
		return error;
	}

	ifnet_unlock();

	/*
	 * Compute offset in the bitmap and deallocate the unit.
	 */
	bytoff = unit >> 3;
	bitoff = unit - (bytoff << 3);
	KASSERT((ifc->ifc_units[bytoff] & (1 << bitoff)) != 0,
	    ("%s: bit is already cleared", __func__));
	ifc->ifc_units[bytoff] &= ~(1 << bitoff);
	return (0);
}

/*
 * Register a network interface cloner.
 */
void
if_clone_attach(struct if_clone *ifc)
{
	int bytoff, bitoff;
	int err;
	int len, maxclone;
	int unit;
	struct if_clone *ifct;

	/* Duplicate entries in if_cloners lead
           to infinite loops in if_clone_create */
	LIST_FOREACH(ifct, &if_cloners, ifc_list) {
		if (ifct == ifc) {
			panic("%s: duplicate entry %s\n",
			      __func__, ifc->ifc_name);
		}
	}

	KASSERT(ifc->ifc_minifs - 1 <= ifc->ifc_maxunit,
	    ("%s: %s requested more units then allowed (%d > %d)",
	    __func__, ifc->ifc_name, ifc->ifc_minifs,
	    ifc->ifc_maxunit + 1));
	/*
	 * Compute bitmap size and allocate it.
	 */
	maxclone = ifc->ifc_maxunit + 1;
	len = maxclone >> 3;
	if ((len << 3) < maxclone)
		len++;
	ifc->ifc_units = kmalloc(len, M_CLONE, M_WAITOK | M_ZERO);
	ifc->ifc_bmlen = len;

	LIST_INSERT_HEAD(&if_cloners, ifc, ifc_list);
	if_cloners_count++;

	for (unit = 0; unit < ifc->ifc_minifs; unit++) {
		err = (*ifc->ifc_create)(ifc, unit, NULL);
		KASSERT(err == 0,
		    ("%s: failed to create required interface %s%d",
		    __func__, ifc->ifc_name, unit));

		/* Allocate the unit in the bitmap. */
		bytoff = unit >> 3;
		bitoff = unit - (bytoff << 3);
		ifc->ifc_units[bytoff] |= (1 << bitoff);
	}
}

/*
 * Unregister a network interface cloner.
 */
void
if_clone_detach(struct if_clone *ifc)
{

	LIST_REMOVE(ifc, ifc_list);
	kfree(ifc->ifc_units, M_CLONE);
	if_cloners_count--;
}

/*
 * Provide list of interface cloners to userspace.
 */
int
if_clone_list(struct if_clonereq *ifcr)
{
	char outbuf[IFNAMSIZ], *dst;
	struct if_clone *ifc;
	int count, error = 0;

	ifcr->ifcr_total = if_cloners_count;
	if ((dst = ifcr->ifcr_buffer) == NULL) {
		/* Just asking how many there are. */
		return (0);
	}

	if (ifcr->ifcr_count < 0)
		return (EINVAL);

	count = (if_cloners_count < ifcr->ifcr_count) ?
	    if_cloners_count : ifcr->ifcr_count;

	for (ifc = LIST_FIRST(&if_cloners); ifc != NULL && count != 0;
	     ifc = LIST_NEXT(ifc, ifc_list), count--, dst += IFNAMSIZ) {
		strlcpy(outbuf, ifc->ifc_name, IFNAMSIZ);
		error = copyout(outbuf, dst, IFNAMSIZ);
		if (error)
			break;
	}

	return (error);
}

/*
 * Look up a network interface cloner.
 */
static struct if_clone *
if_clone_lookup(const char *name, int *unitp)
{
	struct if_clone *ifc;
	const char *cp;
	int i;

	for (ifc = LIST_FIRST(&if_cloners); ifc != NULL;) {
		for (cp = name, i = 0; i < ifc->ifc_namelen; i++, cp++) {
			if (ifc->ifc_name[i] != *cp)
				goto next_ifc;
		}
		goto found_name;
 next_ifc:
		ifc = LIST_NEXT(ifc, ifc_list);
	}

	/* No match. */
	return (NULL);

 found_name:
	if (*cp == '\0') {
		i = -1;
	} else {
		for (i = 0; *cp != '\0'; cp++) {
			if (*cp < '0' || *cp > '9') {
				/* Bogus unit number. */
				return (NULL);
			}
			i = (i * 10) + (*cp - '0');
		}
	}

	if (unitp != NULL)
		*unitp = i;
	return (ifc);
}
