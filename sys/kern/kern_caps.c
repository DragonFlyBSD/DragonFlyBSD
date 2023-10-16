/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/acct.h>
#include <sys/caps.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pioctl.h>
#include <sys/resourcevar.h>
#include <sys/jail.h>
#include <sys/lockf.h>
#include <sys/spinlock.h>
#include <sys/sysctl.h>

#include <sys/spinlock2.h>

__read_mostly static int caps_available = 1;
SYSCTL_INT(_kern, OID_AUTO, caps_available,
	   CTLFLAG_RW, &caps_available, 0 , "");

/*
 * Quick check for cap restriction in cred (no bounds checks),
 * return cap flags.
 */
static __inline
int
caps_check_cred(struct ucred *cred, int cap)
{
	__syscapelm_t elm;

	cap &= ~__SYSCAP_XFLAGS;
	elm = cred->cr_caps.caps[__SYSCAP_INDEX(cap)];

	return ((int)(elm >> __SYSCAP_SHIFT(cap)) & __SYSCAP_ALL);
}

/*
 * int syscap_get(int cap, void *data, size_t bytes);
 */
int
sys_syscap_get(struct sysmsg *sysmsg, const struct syscap_get_args *uap)
{
	struct ucred *cred;
	int cap = uap->cap & ~__SYSCAP_XFLAGS;
	int res;
	int error;

	if (cap < 0)
		return EINVAL;
	if (cap >= __SYSCAP_COUNT)
		return EOPNOTSUPP;
	if (uap->bytes && uap->bytes < sizeof(syscap_base_t))
		return EINVAL;
	error = 0;

	/*
	 * Get capability restriction from parent pid
	 */
	if (uap->cap & __SYSCAP_INPARENT) {
		struct proc *pp;

		pp = pfind(curproc->p_ppid);
		if (pp == NULL)
			return EINVAL;
                lwkt_gettoken_shared(&pp->p_token);	/* protect cred */
		cred = pp->p_ucred;
		crhold(cred);
                lwkt_reltoken(&pp->p_token);
		PRELE(pp);				/* from pfind */
	} else {
		cred = curthread->td_ucred;
	}

	/*
	 * No resource data by default
	 */
	if (uap->data && uap->bytes) {
		syscap_base_t base;

		base.res = SYSCAP_RESOURCE_EOF;
		base.len = sizeof(base);
		error = copyout(&base, uap->data, sizeof(base));
	}

	/*
	 * Get resource bits
	 */
	if (error == 0) {
		res = (int)(cred->cr_caps.caps[__SYSCAP_INDEX(cap)] >>
			    __SYSCAP_SHIFT(cap));
		res &= __SYSCAP_BITS_MASK;
		sysmsg->sysmsg_result = res;
	}

	if (uap->cap & __SYSCAP_INPARENT)
		crfree(cred);

	return error;
}

/*
 * int syscap_set(int cap, int flags, const void *data, size_t bytes)
 */
int
sys_syscap_set(struct sysmsg *sysmsg, const struct syscap_set_args *uap)
{
	struct ucred *cred;
	struct proc *pp;
	int cap = uap->cap & ~__SYSCAP_XFLAGS;
	int res;
	int error;
	int flags = uap->flags;
	__syscapelm_t anymask;

	if (cap < 0 || cap >= __SYSCAP_COUNT)
		return EINVAL;
	if (flags & ~__SYSCAP_BITS_MASK)
		return EINVAL;
	if (uap->data || uap->bytes)
		return EINVAL;
	error = 0;

	/*
	 * Get capability restriction from parent pid.  We can only
	 * mess with the parent if it is running under the same userid
	 * and prison.
	 */
	if (uap->cap & __SYSCAP_INPARENT) {
		pp = pfind(curproc->p_ppid);
		if (pp == NULL)
			return EINVAL;
		if (pp->p_ucred->cr_uid != curproc->p_ucred->cr_uid ||
		    pp->p_ucred->cr_prison != curproc->p_ucred->cr_prison)
		{
			PRELE(pp);		/* from pfind */
			return EINVAL;
		}
	} else {
		pp = curproc;
	}
	lwkt_gettoken(&pp->p_token);		/* protect p_ucred */
	cred = pp->p_ucred;

	/*
	 * Calculate normalized value for requested capability and check
	 * against the stored value.  If they do not match, wire-or to
	 * add the bits and set appropriate SYSCAP_ANY bits indicating
	 * deviation from the root syscaps.
	 */
	res = (int)(cred->cr_caps.caps[__SYSCAP_INDEX(cap)] >>
		    __SYSCAP_SHIFT(cap));
	res &= __SYSCAP_BITS_MASK;

	/*
	 * Handle resource data, if any
	 */

	/*
	 * Set resource bits
	 */
	if (error == 0) {
		if (res != (res | flags)) {
			cred = cratom_proc(pp);
			anymask = (__syscapelm_t)flags <<
				  __SYSCAP_SHIFT(SYSCAP_ANY);
			atomic_set_64(&cred->cr_caps.caps[0], anymask);
			atomic_set_64(&cred->cr_caps.caps[ __SYSCAP_INDEX(cap)],
				      ((__syscapelm_t)uap->flags <<
				       __SYSCAP_SHIFT(cap)));
		}
		sysmsg->sysmsg_result = res | uap->flags;
	}

	/*
	 * Cleanup
	 */
	lwkt_reltoken(&pp->p_token);
	if (uap->cap & __SYSCAP_INPARENT)
		PRELE(pp);			/* from pfind */
	return error;
}

/*
 * Adjust capabilities for exec after the point of no return.
 *
 * This function shifts the EXEC bits into the SELF bits and
 * replicates the EXEC bits.
 */
void
caps_exec(struct proc *p)
{
	struct ucred *cred;
	__syscapelm_t elm;
	int changed = 0;
	int i;

	/*
	 * Dry-run caps inheritance, did anything change?
	 *
	 * caps inheritance basically shifts the EXEC bits into the SELF bits,
	 * and then replicates the EXEC bits.  We have to avoid shifting any
	 * 1's from the SELF bits into the adjacent EXEC bits that may have
	 * previously been 0.
	 */
	cred = p->p_ucred;
	for (i = 0; i < __SYSCAP_NUMELMS; ++i) {
		elm = cred->cr_caps.caps[i];
		elm = ((elm & __SYSCAP_EXECMASK) >> 1) |
		      (elm & __SYSCAP_EXECMASK);
		if (elm != cred->cr_caps.caps[i])
			changed = 1;
	}

	/*
	 * Yes, setup a new ucred for the process
	 */
	if (changed) {
		cratom_proc(p);
		cred = p->p_ucred;
		for (i = 0; i < __SYSCAP_NUMELMS; ++i) {
			elm = cred->cr_caps.caps[i];
			elm = ((elm & __SYSCAP_EXECMASK) >> 1) |
			      (elm & __SYSCAP_EXECMASK);
			cred->cr_caps.caps[i] = elm;
		}
	}
}

/*
 * Return the raw flags for the requested capability.
 */
int
caps_get(struct ucred *cred, int cap)
{
	int res;

	cap &= ~__SYSCAP_XFLAGS;
	if (cap < 0 || cap >= __SYSCAP_COUNT)
		return 0;
	res = (int)(cred->cr_caps.caps[__SYSCAP_INDEX(cap)] >>
		    __SYSCAP_SHIFT(cap));
	res &= __SYSCAP_BITS_MASK;

	return res;
}

/*
 * Set capability restriction bits
 */
void
caps_set_locked(struct proc *p, int cap, int flags)
{
	struct ucred *cred;
	__syscapelm_t elm;

	cap &= ~__SYSCAP_XFLAGS;
	if (cap < 0 || cap >= __SYSCAP_COUNT)
		return;

	cred = cratom_proc(p);
	elm = (__syscapelm_t)flags << __SYSCAP_SHIFT(SYSCAP_ANY);
	atomic_set_64(&cred->cr_caps.caps[0], elm);
	elm = (__syscapelm_t)flags << __SYSCAP_SHIFT(cap);
	atomic_set_64(&cred->cr_caps.caps[ __SYSCAP_INDEX(cap)], elm);
}

/*
 * Returns error code if restricted, 0 on success.
 *
 * These are more sophisticated versions of the baseline caps checks.
 * cr_prison capabilities are also checked, and some capabilities may
 * imply several tests.
 */
int
caps_priv_check(struct ucred *cred, int cap)
{
	int res;

	if (cred == NULL) {
		if (cap & __SYSCAP_NULLCRED)
			return 0;
		return EPERM;
	}

	/*
	 * Uid must be 0 unless NOROOTTEST is requested.  If requested
	 * it means the caller is depending on e.g. /dev/blah perms.
	 */
	if (cred->cr_uid != 0 && (cap & __SYSCAP_NOROOTTEST) == 0)
		return EPERM;

	res = caps_check_cred(cred, cap);
	if (cap & __SYSCAP_GROUP_MASK) {
		cap = (cap & __SYSCAP_GROUP_MASK) >> __SYSCAP_GROUP_SHIFT;
		res |= caps_check_cred(cred, cap);
	}
	if (res & __SYSCAP_SELF)
		return EPERM;
	return (prison_priv_check(cred, cap));
}

int
caps_priv_check_td(thread_t td, int cap)
{
	struct ucred *cred;

	if (td->td_lwp == NULL)			/* not user thread */
		return 0;
	cred = td->td_ucred;
        if (cred == NULL)
		return (EPERM);
						/* must pass restrictions */
	if (caps_check_cred(cred, cap) & __SYSCAP_SELF)
		return EPERM;
	return (prison_priv_check(cred, cap));
}

int
caps_priv_check_self(int cap)
{
	return (caps_priv_check_td(curthread, cap));
}
