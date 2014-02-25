/*-
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and code derived from software contributed to
 * Berkeley by William Jolitz.
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
 *	from: Utah $Hdr: mem.c 1.13 89/10/08$
 *	from: @(#)mem.c	7.2 (Berkeley) 5/9/91
 * $FreeBSD: src/sys/i386/i386/mem.c,v 1.79.2.9 2003/01/04 22:58:01 njl Exp $
 */

/*
 * Memory special file
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/random.h>
#include <sys/signalvar.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>

#include <sys/signal2.h>
#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>


static	d_open_t	mmopen;
static	d_close_t	mmclose;
static	d_read_t	mmread;
static	d_write_t	mmwrite;
static	d_ioctl_t	mmioctl;
static	d_mmap_t	memmmap;
static	d_kqfilter_t	mmkqfilter;

#define CDEV_MAJOR 2
static struct dev_ops mem_ops = {
	{ "mem", 0, D_MPSAFE },
	.d_open =	mmopen,
	.d_close =	mmclose,
	.d_read =	mmread,
	.d_write =	mmwrite,
	.d_ioctl =	mmioctl,
	.d_kqfilter =	mmkqfilter,
	.d_mmap =	memmmap,
};

static int rand_bolt;
static caddr_t	zbuf;
static cdev_t	zerodev = NULL;

MALLOC_DEFINE(M_MEMDESC, "memdesc", "memory range descriptors");
static int mem_ioctl (cdev_t, u_long, caddr_t, int, struct ucred *);
static int random_ioctl (cdev_t, u_long, caddr_t, int, struct ucred *);

struct mem_range_softc mem_range_softc;

static int seedenable;
SYSCTL_INT(_kern, OID_AUTO, seedenable, CTLFLAG_RW, &seedenable, 0, "");

static int
mmopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error;

	switch (minor(dev)) {
	case 0:
	case 1:
		if (ap->a_oflags & FWRITE) {
			if (securelevel > 0 || kernel_mem_readonly)
				return (EPERM);
		}
		error = 0;
		break;
	case 14:
		error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
		if (error != 0)
			break;
		if (securelevel > 0 || kernel_mem_readonly) {
			error = EPERM;
			break;
		}
		error = cpu_set_iopl();
		break;
	default:
		error = 0;
		break;
	}
	return (error);
}

static int
mmclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error;

	switch (minor(dev)) {
	case 14:
		error = cpu_clr_iopl();
		break;
	default:
		error = 0;
		break;
	}
	return (error);
}


static int
mmrw(cdev_t dev, struct uio *uio, int flags)
{
	int o;
	u_int c;
	u_int poolsize;
	u_long v;
	struct iovec *iov;
	int error = 0;
	caddr_t buf = NULL;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (minor(dev)) {
		case 0:
			/*
			 * minor device 0 is physical memory, /dev/mem 
			 */
			v = uio->uio_offset;
			v &= ~(long)PAGE_MASK;
			pmap_kenter((vm_offset_t)ptvmmap, v);
			o = (int)uio->uio_offset & PAGE_MASK;
			c = (u_int)(PAGE_SIZE - ((uintptr_t)iov->iov_base & PAGE_MASK));
			c = min(c, (u_int)(PAGE_SIZE - o));
			c = min(c, (u_int)iov->iov_len);
			error = uiomove((caddr_t)&ptvmmap[o], (int)c, uio);
			pmap_kremove((vm_offset_t)ptvmmap);
			continue;

		case 1: {
			/*
			 * minor device 1 is kernel memory, /dev/kmem 
			 */
			vm_offset_t saddr, eaddr;
			int prot;

			c = iov->iov_len;

			/*
			 * Make sure that all of the pages are currently 
			 * resident so that we don't create any zero-fill
			 * pages.
			 */
			saddr = trunc_page(uio->uio_offset);
			eaddr = round_page(uio->uio_offset + c);
			if (saddr > eaddr)
				return EFAULT;

			/*
			 * Make sure the kernel addresses are mapped.
			 * platform_direct_mapped() can be used to bypass
			 * default mapping via the page table (virtual kernels
			 * contain a lot of out-of-band data).
			 */
			prot = VM_PROT_READ;
			if (uio->uio_rw != UIO_READ)
				prot |= VM_PROT_WRITE;
			error = kvm_access_check(saddr, eaddr, prot);
			if (error)
				return (error);
			error = uiomove((caddr_t)(vm_offset_t)uio->uio_offset,
					(int)c, uio);
			continue;
		}
		case 2:
			/*
			 * minor device 2 (/dev/null) is EOF/RATHOLE
			 */
			if (uio->uio_rw == UIO_READ)
				return (0);
			c = iov->iov_len;
			break;
		case 3:
			/*
			 * minor device 3 (/dev/random) is source of filth
			 * on read, seeder on write
			 */
			if (buf == NULL)
				buf = kmalloc(PAGE_SIZE, M_TEMP, M_WAITOK);
			c = min(iov->iov_len, PAGE_SIZE);
			if (uio->uio_rw == UIO_WRITE) {
				error = uiomove(buf, (int)c, uio);
				if (error == 0 &&
				    seedenable &&
				    securelevel <= 0) {
					error = add_buffer_randomness(buf, c);
				} else if (error == 0) {
					error = EPERM;
				}
			} else {
				poolsize = read_random(buf, c);
				if (poolsize == 0) {
					if (buf)
						kfree(buf, M_TEMP);
					if ((flags & IO_NDELAY) != 0)
						return (EWOULDBLOCK);
					return (0);
				}
				c = min(c, poolsize);
				error = uiomove(buf, (int)c, uio);
			}
			continue;
		case 4:
			/*
			 * minor device 4 (/dev/urandom) is source of muck
			 * on read, writes are disallowed.
			 */
			c = min(iov->iov_len, PAGE_SIZE);
			if (uio->uio_rw == UIO_WRITE) {
				error = EPERM;
				break;
			}
			if (CURSIG(curthread->td_lwp) != 0) {
				/*
				 * Use tsleep() to get the error code right.
				 * It should return immediately.
				 */
				error = tsleep(&rand_bolt, PCATCH, "urand", 1);
				if (error != 0 && error != EWOULDBLOCK)
					continue;
			}
			if (buf == NULL)
				buf = kmalloc(PAGE_SIZE, M_TEMP, M_WAITOK);
			poolsize = read_random_unlimited(buf, c);
			c = min(c, poolsize);
			error = uiomove(buf, (int)c, uio);
			continue;
		case 12:
			/*
			 * minor device 12 (/dev/zero) is source of nulls 
			 * on read, write are disallowed.
			 */
			if (uio->uio_rw == UIO_WRITE) {
				c = iov->iov_len;
				break;
			}
			if (zbuf == NULL) {
				zbuf = (caddr_t)kmalloc(PAGE_SIZE, M_TEMP,
				    M_WAITOK | M_ZERO);
			}
			c = min(iov->iov_len, PAGE_SIZE);
			error = uiomove(zbuf, (int)c, uio);
			continue;
		default:
			return (ENODEV);
		}
		if (error)
			break;
		iov->iov_base = (char *)iov->iov_base + c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}
	if (buf)
		kfree(buf, M_TEMP);
	return (error);
}

static int
mmread(struct dev_read_args *ap)
{
	return(mmrw(ap->a_head.a_dev, ap->a_uio, ap->a_ioflag));
}

static int
mmwrite(struct dev_write_args *ap)
{
	return(mmrw(ap->a_head.a_dev, ap->a_uio, ap->a_ioflag));
}





/*******************************************************\
* allow user processes to MMAP some memory sections	*
* instead of going through read/write			*
\*******************************************************/

static int
memmmap(struct dev_mmap_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	switch (minor(dev)) {
	case 0:
		/* 
		 * minor device 0 is physical memory 
		 */
#if defined(__i386__)
        	ap->a_result = i386_btop(ap->a_offset);
#elif defined(__x86_64__)
		ap->a_result = x86_64_btop(ap->a_offset);
#endif
		return 0;
	case 1:
		/*
		 * minor device 1 is kernel memory 
		 */
#if defined(__i386__)
        	ap->a_result = i386_btop(vtophys(ap->a_offset));
#elif defined(__x86_64__)
        	ap->a_result = x86_64_btop(vtophys(ap->a_offset));
#endif
		return 0;

	default:
		return EINVAL;
	}
}

static int
mmioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error;

	get_mplock();

	switch (minor(dev)) {
	case 0:
		error = mem_ioctl(dev, ap->a_cmd, ap->a_data,
				  ap->a_fflag, ap->a_cred);
		break;
	case 3:
	case 4:
		error = random_ioctl(dev, ap->a_cmd, ap->a_data,
				     ap->a_fflag, ap->a_cred);
		break;
	default:
		error = ENODEV;
		break;
	}

	rel_mplock();
	return (error);
}

/*
 * Operations for changing memory attributes.
 *
 * This is basically just an ioctl shim for mem_range_attr_get
 * and mem_range_attr_set.
 */
static int 
mem_ioctl(cdev_t dev, u_long cmd, caddr_t data, int flags, struct ucred *cred)
{
	int nd, error = 0;
	struct mem_range_op *mo = (struct mem_range_op *)data;
	struct mem_range_desc *md;
	
	/* is this for us? */
	if ((cmd != MEMRANGE_GET) &&
	    (cmd != MEMRANGE_SET))
		return (ENOTTY);

	/* any chance we can handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	/* do we have any descriptors? */
	if (mem_range_softc.mr_ndesc == 0)
		return (ENXIO);

	switch (cmd) {
	case MEMRANGE_GET:
		nd = imin(mo->mo_arg[0], mem_range_softc.mr_ndesc);
		if (nd > 0) {
			md = (struct mem_range_desc *)
				kmalloc(nd * sizeof(struct mem_range_desc),
				       M_MEMDESC, M_WAITOK);
			error = mem_range_attr_get(md, &nd);
			if (!error)
				error = copyout(md, mo->mo_desc, 
					nd * sizeof(struct mem_range_desc));
			kfree(md, M_MEMDESC);
		} else {
			nd = mem_range_softc.mr_ndesc;
		}
		mo->mo_arg[0] = nd;
		break;
		
	case MEMRANGE_SET:
		md = (struct mem_range_desc *)kmalloc(sizeof(struct mem_range_desc),
						    M_MEMDESC, M_WAITOK);
		error = copyin(mo->mo_desc, md, sizeof(struct mem_range_desc));
		/* clamp description string */
		md->mr_owner[sizeof(md->mr_owner) - 1] = 0;
		if (error == 0)
			error = mem_range_attr_set(md, &mo->mo_arg[0]);
		kfree(md, M_MEMDESC);
		break;
	}
	return (error);
}

/*
 * Implementation-neutral, kernel-callable functions for manipulating
 * memory range attributes.
 */
int
mem_range_attr_get(struct mem_range_desc *mrd, int *arg)
{
	/* can we handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	if (*arg == 0) {
		*arg = mem_range_softc.mr_ndesc;
	} else {
		bcopy(mem_range_softc.mr_desc, mrd, (*arg) * sizeof(struct mem_range_desc));
	}
	return (0);
}

int
mem_range_attr_set(struct mem_range_desc *mrd, int *arg)
{
	/* can we handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	return (mem_range_softc.mr_op->set(&mem_range_softc, mrd, arg));
}

void
mem_range_AP_init(void)
{
	if (mem_range_softc.mr_op && mem_range_softc.mr_op->initAP)
		mem_range_softc.mr_op->initAP(&mem_range_softc);
}

static int 
random_ioctl(cdev_t dev, u_long cmd, caddr_t data, int flags, struct ucred *cred)
{
	int error;
	int intr;
	
	/*
	 * Even inspecting the state is privileged, since it gives a hint
	 * about how easily the randomness might be guessed.
	 */
	error = 0;

	switch (cmd) {
	/* Really handled in upper layer */
	case FIOASYNC:
		break;
	case MEM_SETIRQ:
		intr = *(int16_t *)data;
		if ((error = priv_check_cred(cred, PRIV_ROOT, 0)) != 0)
			break;
		if (intr < 0 || intr >= MAX_INTS)
			return (EINVAL);
		register_randintr(intr);
		break;
	case MEM_CLEARIRQ:
		intr = *(int16_t *)data;
		if ((error = priv_check_cred(cred, PRIV_ROOT, 0)) != 0)
			break;
		if (intr < 0 || intr >= MAX_INTS)
			return (EINVAL);
		unregister_randintr(intr);
		break;
	case MEM_RETURNIRQ:
		error = ENOTSUP;
		break;
	case MEM_FINDIRQ:
		intr = *(int16_t *)data;
		if ((error = priv_check_cred(cred, PRIV_ROOT, 0)) != 0)
			break;
		if (intr < 0 || intr >= MAX_INTS)
			return (EINVAL);
		intr = next_registered_randintr(intr);
		if (intr == MAX_INTS)
			return (ENOENT);
		*(u_int16_t *)data = intr;
		break;
	default:
		error = ENOTSUP;
		break;
	}
	return (error);
}

static int
mm_filter_read(struct knote *kn, long hint)
{
	return (1);
}

static int
mm_filter_write(struct knote *kn, long hint)
{
	return (1);
}

static void
dummy_filter_detach(struct knote *kn) {}

/* Implemented in kern_nrandom.c */
static struct filterops random_read_filtops =
        { FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, dummy_filter_detach, random_filter_read };

static struct filterops mm_read_filtops =
        { FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, dummy_filter_detach, mm_filter_read };

static struct filterops mm_write_filtops =
        { FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, dummy_filter_detach, mm_filter_write };

int
mmkqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	cdev_t dev = ap->a_head.a_dev;

	ap->a_result = 0;
	switch (kn->kn_filter) {
	case EVFILT_READ:
		switch (minor(dev)) {
		case 3:
			kn->kn_fop = &random_read_filtops;
			break;
		default:
			kn->kn_fop = &mm_read_filtops;
			break;
		}
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &mm_write_filtops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	return (0);
}

int
iszerodev(cdev_t dev)
{
	return (zerodev == dev);
}

static void
mem_drvinit(void *unused)
{

	/* Initialise memory range handling */
	if (mem_range_softc.mr_op != NULL)
		mem_range_softc.mr_op->init(&mem_range_softc);

	make_dev(&mem_ops, 0, UID_ROOT, GID_KMEM, 0640, "mem");
	make_dev(&mem_ops, 1, UID_ROOT, GID_KMEM, 0640, "kmem");
	make_dev(&mem_ops, 2, UID_ROOT, GID_WHEEL, 0666, "null");
	make_dev(&mem_ops, 3, UID_ROOT, GID_WHEEL, 0644, "random");
	make_dev(&mem_ops, 4, UID_ROOT, GID_WHEEL, 0644, "urandom");
	zerodev = make_dev(&mem_ops, 12, UID_ROOT, GID_WHEEL, 0666, "zero");
	make_dev(&mem_ops, 14, UID_ROOT, GID_WHEEL, 0600, "io");
}

SYSINIT(memdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,mem_drvinit,NULL)

