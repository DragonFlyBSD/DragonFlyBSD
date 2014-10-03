/*
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_subr.c	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_subr.c,v 1.31.2.2 2002/04/21 08:09:37 bde Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/thread2.h>
#include <machine/limits.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>

SYSCTL_INT(_kern, KERN_IOV_MAX, iov_max, CTLFLAG_RD, NULL, UIO_MAXIOV,
	"Maximum number of elements in an I/O vector; sysconf(_SC_IOV_MAX)");

int
copyin_nofault(const void *udaddr, void *kaddr, size_t len)
{
	thread_t td = curthread;
	int error;

	atomic_set_int(&td->td_flags, TDF_NOFAULT);
	error = copyin(udaddr, kaddr, len);
	atomic_clear_int(&td->td_flags, TDF_NOFAULT);
	return error;
}

int
copyout_nofault(const void *kaddr, void *udaddr, size_t len)
{
	thread_t td = curthread;
	int error;

	atomic_set_int(&td->td_flags, TDF_NOFAULT);
	error = copyout(kaddr, udaddr, len);
	atomic_clear_int(&td->td_flags, TDF_NOFAULT);
	return error;
}

/*
 * UIO_READ:	copy the kernelspace cp to the user or kernelspace UIO
 * UIO_WRITE:	copy the user or kernelspace UIO to the kernelspace cp
 *
 * For userspace UIO's, uio_td must be the current thread.
 *
 * The syscall interface is responsible for limiting the length to
 * ssize_t for things like read() or write() which return the bytes
 * read or written as ssize_t.  These functions work with unsigned
 * lengths.
 */
int
uiomove(caddr_t cp, size_t n, struct uio *uio)
{
	thread_t td = curthread;
	struct iovec *iov;
	size_t cnt;
	size_t tot;
	int error = 0;
	int save = 0;

	KASSERT(uio->uio_rw == UIO_READ || uio->uio_rw == UIO_WRITE,
	    ("uiomove: mode"));
	KASSERT(uio->uio_segflg != UIO_USERSPACE || uio->uio_td == td,
	    ("uiomove proc"));

	crit_enter();
	save = td->td_flags & TDF_DEADLKTREAT;
	td->td_flags |= TDF_DEADLKTREAT;
	crit_exit();

	tot = 0;

	while (n > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (cnt > n)
			cnt = n;
		tot += cnt;

		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
			if (tot > 1024*1024)
				lwkt_user_yield();
			if (uio->uio_rw == UIO_READ)
				error = copyout(cp, iov->iov_base, cnt);
			else
				error = copyin(iov->iov_base, cp, cnt);
			break;
		case UIO_SYSSPACE:
			if (uio->uio_rw == UIO_READ)
				bcopy(cp, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, cp, cnt);
			break;
		case UIO_NOCOPY:
			break;
		}

		if (error)
			break;
		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		cp += cnt;
		n -= cnt;
	}
	crit_enter();
	td->td_flags = (td->td_flags & ~TDF_DEADLKTREAT) | save;
	crit_exit();

	return (error);
}

/*
 * This is the same as uiomove() except (cp, n) is within the bounds of
 * the passed, locked buffer.  Under certain circumstances a VM fault
 * occuring with a locked buffer held can result in a deadlock or an
 * attempt to recursively lock the buffer.
 *
 * This procedure deals with these cases.
 *
 * If the buffer represents a regular file, is B_CACHE, but the last VM page
 * is not fully valid we fix-up the last VM page.  This should handle the
 * recursive lock issue.
 *
 * Deadlocks are another issue.  We are holding the vp and the bp locked
 * and could deadlock against a different vp and/or bp if another thread is
 * trying to access us while we accessing it.  The only solution here is
 * to release the bp and vnode lock and do the uio to/from a system buffer,
 * then regain the locks and copyback (if applicable).  XXX TODO.
 */
int
uiomovebp(struct buf *bp, caddr_t cp, size_t n, struct uio *uio)
{
	int count;
	vm_page_t m;

	if (bp->b_vp && bp->b_vp->v_type == VREG &&
	    (bp->b_flags & B_CACHE) &&
	    (count = bp->b_xio.xio_npages) != 0 &&
	    (m = bp->b_xio.xio_pages[count-1])->valid != VM_PAGE_BITS_ALL) {
		vm_page_zero_invalid(m, TRUE);
	}
	return (uiomove(cp, n, uio));
}

/*
 * uiomove() but fail for non-trivial VM faults, even if the VM fault is
 * valid.  Returns EFAULT if a VM fault occurred via the copyin/copyout
 * onfault code.
 *
 * This allows callers to hold e.g. a busy VM page, or a busy VM object,
 * or a locked vnode through the call and then fall-back to safer code
 * if we fail.
 */
int
uiomove_nofault(caddr_t cp, size_t n, struct uio *uio)
{
	thread_t td = curthread;
	int error;

	atomic_set_int(&td->td_flags, TDF_NOFAULT);
	error = uiomove(cp, n, uio);
	atomic_clear_int(&td->td_flags, TDF_NOFAULT);
	return error;
}

/*
 * Like uiomove() but copies zero-fill.  Only allowed for UIO_READ,
 * for obvious reasons.
 */
int
uiomovez(size_t n, struct uio *uio)
{
	struct iovec *iov;
	size_t cnt;
	int error = 0;

	KASSERT(uio->uio_rw == UIO_READ, ("uiomovez: mode"));
	KASSERT(uio->uio_segflg != UIO_USERSPACE || uio->uio_td == curthread,
		("uiomove proc"));

	while (n > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (cnt > n)
			cnt = n;

		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
			error = copyout(ZeroPage, iov->iov_base, cnt);
			break;
		case UIO_SYSSPACE:
			bzero(iov->iov_base, cnt);
			break;
		case UIO_NOCOPY:
			break;
		}

		if (error)
			break;
		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		n -= cnt;
	}
	return (error);
}

/*
 * Wrapper for uiomove() that validates the arguments against a known-good
 * kernel buffer.  This function automatically indexes the buffer by
 * uio_offset and handles all range checking.
 */
int
uiomove_frombuf(void *buf, size_t buflen, struct uio *uio)
{
	size_t offset;

	offset = (size_t)uio->uio_offset;
	if ((off_t)offset != uio->uio_offset)
		return (EINVAL);
	if (buflen == 0 || offset >= buflen)
		return (0);
	return (uiomove((char *)buf + offset, buflen - offset, uio));
}

/*
 * Give next character to user as result of read.
 */
int
ureadc(int c, struct uio *uio)
{
	struct iovec *iov;
	char *iov_base;

again:
	if (uio->uio_iovcnt == 0 || uio->uio_resid == 0)
		panic("ureadc");
	iov = uio->uio_iov;
	if (iov->iov_len == 0) {
		uio->uio_iovcnt--;
		uio->uio_iov++;
		goto again;
	}

	switch (uio->uio_segflg) {
	case UIO_USERSPACE:
		if (subyte(iov->iov_base, c) < 0)
			return (EFAULT);
		break;
	case UIO_SYSSPACE:
		iov_base = iov->iov_base;
		*iov_base = c;
		iov->iov_base = iov_base;
		break;
	case UIO_NOCOPY:
		break;
	}

	iov->iov_base = (char *)iov->iov_base + 1;
	iov->iov_len--;
	uio->uio_resid--;
	uio->uio_offset++;
	return (0);
}

/*
 * General routine to allocate a hash table.  Make the hash table size a
 * power of 2 greater or equal to the number of elements requested, and 
 * store the masking value in *hashmask.
 */
void *
hashinit(int elements, struct malloc_type *type, u_long *hashmask)
{
	long hashsize;
	LIST_HEAD(generic, generic) *hashtbl;
	int i;

	if (elements <= 0)
		panic("hashinit: bad elements");
	for (hashsize = 2; hashsize < elements; hashsize <<= 1)
		continue;
	hashtbl = kmalloc((u_long)hashsize * sizeof(*hashtbl), type, M_WAITOK);
	for (i = 0; i < hashsize; i++)
		LIST_INIT(&hashtbl[i]);
	*hashmask = hashsize - 1;
	return (hashtbl);
}

void
hashdestroy(void *vhashtbl, struct malloc_type *type, u_long hashmask)
{
	LIST_HEAD(generic, generic) *hashtbl, *hp;

	hashtbl = vhashtbl;
	for (hp = hashtbl; hp <= &hashtbl[hashmask]; hp++)
		KASSERT(LIST_EMPTY(hp), ("%s: hash not empty", __func__));
	kfree(hashtbl, type);
}

/*
 * This is a newer version which allocates a hash table of structures.
 *
 * The returned array will be zero'd.  The caller is responsible for
 * initializing the structures.
 */
void *
hashinit_ext(int elements, size_t size, struct malloc_type *type,
	     u_long *hashmask)
{
	long hashsize;
	void *hashtbl;

	if (elements <= 0)
		panic("hashinit: bad elements");
	for (hashsize = 2; hashsize < elements; hashsize <<= 1)
		continue;
	hashtbl = kmalloc((size_t)hashsize * size, type, M_WAITOK | M_ZERO);
	*hashmask = hashsize - 1;
	return (hashtbl);
}

static int primes[] = { 1, 13, 31, 61, 127, 251, 509, 761, 1021, 1531, 2039,
			2557, 3067, 3583, 4093, 4603, 5119, 5623, 6143, 6653,
			7159, 7673, 8191, 12281, 16381, 24571, 32749 };
#define NPRIMES NELEM(primes)

/*
 * General routine to allocate a prime number sized hash table.
 */
void *
phashinit(int elements, struct malloc_type *type, u_long *nentries)
{
	long hashsize;
	LIST_HEAD(generic, generic) *hashtbl;
	int i;

	if (elements <= 0)
		panic("phashinit: bad elements");
	for (i = 1, hashsize = primes[1]; hashsize <= elements;) {
		i++;
		if (i == NPRIMES)
			break;
		hashsize = primes[i];
	}
	hashsize = primes[i - 1];
	hashtbl = kmalloc((u_long)hashsize * sizeof(*hashtbl), type, M_WAITOK);
	for (i = 0; i < hashsize; i++)
		LIST_INIT(&hashtbl[i]);
	*nentries = hashsize;
	return (hashtbl);
}

/*
 * This is a newer version which allocates a hash table of structures
 * in a prime-number size.
 *
 * The returned array will be zero'd.  The caller is responsible for
 * initializing the structures.
 */
void *
phashinit_ext(int elements, size_t size, struct malloc_type *type,
	      u_long *nentries)
{
	long hashsize;
	void *hashtbl;
	int i;

	if (elements <= 0)
		panic("phashinit: bad elements");
	for (i = 1, hashsize = primes[1]; hashsize <= elements;) {
		i++;
		if (i == NPRIMES)
			break;
		hashsize = primes[i];
	}
	hashsize = primes[i - 1];
	hashtbl = kmalloc((size_t)hashsize * size, type, M_WAITOK | M_ZERO);
	*nentries = hashsize;
	return (hashtbl);
}

/*
 * Copyin an iovec.  If the iovec array fits, use the preallocated small
 * iovec structure.  If it is too big, dynamically allocate an iovec array
 * of sufficient size.
 *
 * MPSAFE
 */
int
iovec_copyin(struct iovec *uiov, struct iovec **kiov, struct iovec *siov,
	     size_t iov_cnt, size_t *iov_len)
{
	struct iovec *iovp;
	int error, i;
	size_t len;

	if (iov_cnt > UIO_MAXIOV)
		return EMSGSIZE;
	if (iov_cnt > UIO_SMALLIOV) {
		*kiov = kmalloc(sizeof(struct iovec) * iov_cnt, M_IOV,
				M_WAITOK);
	} else {
		*kiov = siov;
	}
	error = copyin(uiov, *kiov, iov_cnt * sizeof(struct iovec));
	if (error == 0) {
		*iov_len = 0;
		for (i = 0, iovp = *kiov; i < iov_cnt; i++, iovp++) {
			/*
			 * Check for both *iov_len overflows and out of
			 * range iovp->iov_len's.  We limit to the
			 * capabilities of signed integers.
			 *
			 * GCC4 - overflow check opt requires assign/test.
			 */
			len = *iov_len + iovp->iov_len;
			if (len < *iov_len)
				error = EINVAL;
			*iov_len = len;
		}
	}

	/*
	 * From userland disallow iovec's which exceed the sized size
	 * limit as the system calls return ssize_t.
	 *
	 * NOTE: Internal kernel interfaces can handle the unsigned
	 *	 limit.
	 */
	if (error == 0 && (ssize_t)*iov_len < 0)
		error = EINVAL;

	if (error)
		iovec_free(kiov, siov);
	return (error);
}


/*
 * Copyright (c) 2004 Alan L. Cox <alc@cs.rice.edu>
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#)kern_subr.c	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/i386/i386/uio_machdep.c,v 1.1 2004/03/21 20:28:36 alc Exp $
 */

/*
 * Implement uiomove(9) from physical memory using lwbuf's to reduce
 * the creation and destruction of ephemeral mappings.
 */
int
uiomove_fromphys(vm_page_t *ma, vm_offset_t offset, size_t n, struct uio *uio)
{
	struct lwbuf lwb_cache;
	struct lwbuf *lwb;
	struct thread *td = curthread;
	struct iovec *iov;
	void *cp;
	vm_offset_t page_offset;
	vm_page_t m;
	size_t cnt;
	int error = 0;
	int save = 0;

	KASSERT(uio->uio_rw == UIO_READ || uio->uio_rw == UIO_WRITE,
	    ("uiomove_fromphys: mode"));
	KASSERT(uio->uio_segflg != UIO_USERSPACE || uio->uio_td == curthread,
	    ("uiomove_fromphys proc"));

	crit_enter();
	save = td->td_flags & TDF_DEADLKTREAT;
	td->td_flags |= TDF_DEADLKTREAT;
	crit_exit();

	while (n > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (cnt > n)
			cnt = n;
		page_offset = offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - page_offset);
		m = ma[offset >> PAGE_SHIFT];
		lwb = lwbuf_alloc(m, &lwb_cache);
		cp = (char *)lwbuf_kva(lwb) + page_offset;

		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
			/*
			 * note: removed uioyield (it was the wrong place to
			 * put it).
			 */
			if (uio->uio_rw == UIO_READ)
				error = copyout(cp, iov->iov_base, cnt);
			else
				error = copyin(iov->iov_base, cp, cnt);
			if (error) {
				lwbuf_free(lwb);
				goto out;
			}
			break;
		case UIO_SYSSPACE:
			if (uio->uio_rw == UIO_READ)
				bcopy(cp, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, cp, cnt);
			break;
		case UIO_NOCOPY:
			break;
		}
		lwbuf_free(lwb);
		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		offset += cnt;
		n -= cnt;
	}
out:
	if (save == 0) {
		crit_enter();
		td->td_flags &= ~TDF_DEADLKTREAT;
		crit_exit();
	}
	return (error);
}
