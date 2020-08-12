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
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/eventhandler.h>
#include <sys/limits.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>

static LIST_HEAD(, if_clone) if_cloners = LIST_HEAD_INITIALIZER(if_cloners);
static int if_cloners_count;

MALLOC_DEFINE(M_CLONE, "clone", "interface cloning framework");

static int	if_name2unit(const char *, int *);
static bool	if_clone_match(struct if_clone *, const char *);
static struct if_clone *if_clone_lookup(const char *);
static int	if_clone_alloc_unit(struct if_clone *, int *);
static void	if_clone_free_unit(struct if_clone *, int);
static int	if_clone_createif(struct if_clone *, int, caddr_t, caddr_t);

/*
 * Lookup the cloner and create a clone network interface.
 */
int
if_clone_create(char *name, int len, caddr_t params, caddr_t data)
{
	struct if_clone *ifc;
	char ifname[IFNAMSIZ];
	bool wildcard;
	int unit;
	int err;

	if ((ifc = if_clone_lookup(name)) == NULL)
		return (EINVAL);
	if ((err = if_name2unit(name, &unit)) != 0)
		return (err);

	wildcard = (unit < 0);

	ifnet_lock();
	if ((err = if_clone_alloc_unit(ifc, &unit)) != 0) {
		ifnet_unlock();
		return (err);
	}

	ksnprintf(ifname, IFNAMSIZ, "%s%d", ifc->ifc_name, unit);

	/*
	 * Update the name with the allocated unit for the caller,
	 * who must preserve enough space.
	 */
	if (wildcard && strlcpy(name, ifname, len) >= len) {
		if_clone_free_unit(ifc, unit);
		ifnet_unlock();
		return (ENOSPC);
	}

	err = if_clone_createif(ifc, unit, params, data);
	if (err)
		if_clone_free_unit(ifc, unit);
	ifnet_unlock();

	return (err);
}

/*
 * Lookup the cloner and destroy a clone network interface.
 */
int
if_clone_destroy(const char *name)
{
	struct if_clone *ifc;
	struct ifnet *ifp;
	int unit, error;

	ifnet_lock();
	ifp = ifunit(name);
	ifnet_unlock();
	if (ifp == NULL)
		return (ENXIO);

	if ((ifc = if_clone_lookup(ifp->if_dname)) == NULL)
		return (EINVAL);

	unit = ifp->if_dunit;
	if (unit < ifc->ifc_minifs)
		return (EINVAL);

	if (ifc->ifc_destroy == NULL)
		return (EOPNOTSUPP);

	ifnet_lock();
	if_clone_free_unit(ifc, unit);
	error = ifc->ifc_destroy(ifp);
	if (error)
		if_clone_alloc_unit(ifc, &unit);
	/* else ifc structure is dead */
	ifnet_unlock();

	return (error);
}

/*
 * Register a network interface cloner.
 */
int
if_clone_attach(struct if_clone *ifc)
{
	struct if_clone *ifct;
	int len, maxclone;
	int unit;

	LIST_FOREACH(ifct, &if_cloners, ifc_list) {
		if (strcmp(ifct->ifc_name, ifc->ifc_name) == 0)
			return (EEXIST);
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

	ifnet_lock();
	for (unit = 0; unit < ifc->ifc_minifs; unit++) {
		if_clone_alloc_unit(ifc, &unit);
		if (if_clone_createif(ifc, unit, NULL, NULL) != 0) {
			ifnet_unlock();
			panic("%s: failed to create required interface %s%d",
			      __func__, ifc->ifc_name, unit);
		}
	}
	ifnet_unlock();

	EVENTHANDLER_INVOKE(if_clone_event, ifc);

	return (0);
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

	for (ifc = LIST_FIRST(&if_cloners);
	     ifc != NULL && count != 0;
	     ifc = LIST_NEXT(ifc, ifc_list), count--, dst += IFNAMSIZ) {
		bzero(outbuf, IFNAMSIZ);	/* sanitize */
		strlcpy(outbuf, ifc->ifc_name, IFNAMSIZ);
		error = copyout(outbuf, dst, IFNAMSIZ);
		if (error)
			break;
	}

	return (error);
}

/*
 * Extract the unit number from interface name of the form "name###".
 * A unit of -1 is stored if the given name doesn't have a unit.
 *
 * Returns 0 on success and an error on failure.
 */
static int
if_name2unit(const char *name, int *unit)
{
	const char *cp;
	int cutoff = INT_MAX / 10;
	int cutlim = INT_MAX % 10;

	for (cp = name; *cp != '\0' && (*cp < '0' || *cp > '9'); cp++)
		;
	if (*cp == '\0') {
		*unit = -1;
	} else if (cp[0] == '0' && cp[1] != '\0') {
		/* Disallow leading zeroes. */
		return (EINVAL);
	} else {
		for (*unit = 0; *cp != '\0'; cp++) {
			if (*cp < '0' || *cp > '9') {
				/* Bogus unit number. */
				return (EINVAL);
			}
			if (*unit > cutoff ||
			    (*unit == cutoff && *cp - '0' > cutlim))
				return (EINVAL);
			*unit = (*unit * 10) + (*cp - '0');
		}
	}

	return (0);
}

/*
 * Check whether the interface cloner matches the name.
 */
static bool
if_clone_match(struct if_clone *ifc, const char *name)
{
	const char *cp;
	int i;

	/* Match the name */
	for (cp = name, i = 0; i < strlen(ifc->ifc_name); i++, cp++) {
		if (ifc->ifc_name[i] != *cp)
			return (false);
	}

	/* Make sure there's a unit number or nothing after the name */
	for ( ; *cp != '\0'; cp++) {
		if (*cp < '0' || *cp > '9')
			return (false);
	}

	return (true);
}

/*
 * Look up a network interface cloner.
 */
static struct if_clone *
if_clone_lookup(const char *name)
{
	struct if_clone *ifc;

	LIST_FOREACH(ifc, &if_cloners, ifc_list) {
		if (if_clone_match(ifc, name))
			return ifc;
	}

	return (NULL);
}

/*
 * Allocate a unit number.
 *
 * ifnet must be locked.
 *
 * Returns 0 on success and an error on failure.
 */
static int
if_clone_alloc_unit(struct if_clone *ifc, int *unit)
{
	int bytoff, bitoff;

	if (*unit < 0) {
		/*
		 * Wildcard mode: find a free unit.
		 */
		bytoff = bitoff = 0;
		while (bytoff < ifc->ifc_bmlen &&
		       ifc->ifc_units[bytoff] == 0xff)
			bytoff++;
		if (bytoff >= ifc->ifc_bmlen)
			return (ENOSPC);
		while ((ifc->ifc_units[bytoff] & (1 << bitoff)) != 0)
			bitoff++;
		*unit = (bytoff << 3) + bitoff;
	} else {
		bytoff = *unit >> 3;
		bitoff = *unit - (bytoff << 3);
	}

	if (*unit > ifc->ifc_maxunit)
		return (ENXIO);

	/*
	 * Allocate the unit in the bitmap.
	 */
#if 0
	KASSERT((ifc->ifc_units[bytoff] & (1 << bitoff)) == 0,
		("%s: bit is already set", __func__));
#endif
	if (ifc->ifc_units[bytoff] & (1 << bitoff))
		return (EEXIST);
	ifc->ifc_units[bytoff] |= (1 << bitoff);

	return (0);
}

/*
 * Free an allocated unit number.
 *
 * ifnet must be locked.
 */
static void
if_clone_free_unit(struct if_clone *ifc, int unit)
{
	int bytoff, bitoff;

	bytoff = unit >> 3;
	bitoff = unit - (bytoff << 3);
	KASSERT((ifc->ifc_units[bytoff] & (1 << bitoff)) != 0,
		("%s: bit is already cleared", __func__));
	ifc->ifc_units[bytoff] &= ~(1 << bitoff);
}

/*
 * Create a clone network interface.
 *
 * ifnet must be locked
 */
static int
if_clone_createif(struct if_clone *ifc, int unit, caddr_t params, caddr_t data)
{
	struct ifnet *ifp;
	char ifname[IFNAMSIZ];
	int err;

	ksnprintf(ifname, IFNAMSIZ, "%s%d", ifc->ifc_name, unit);

	ifp = ifunit(ifname);
	if (ifp != NULL)
		return (EEXIST);

	/*ifnet_unlock();*/
	err = (*ifc->ifc_create)(ifc, unit, params, data);
	/*ifnet_lock();*/
	if (err != 0)
		return (err);

	ifp = ifunit(ifname);
	if (ifp == NULL)
		return (ENXIO);

	err = if_addgroup(ifp, ifc->ifc_name);
	if (err != 0)
		return (err);

	return (0);
}
