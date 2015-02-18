/*
 * Copyright (c) 1996 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * $FreeBSD: src/sys/kern/sys_pipe.c,v 1.60.2.13 2002/08/05 15:05:15 des Exp $
 */

/*
 * This file contains a high-performance replacement for the socket-based
 * pipes scheme originally used in FreeBSD/4.4Lite.  It does not support
 * all features of sockets, but does do everything that pipes normally
 * do.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/stat.h>
#include <sys/signalvar.h>
#include <sys/sysproto.h>
#include <sys/pipe.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/event.h>
#include <sys/globaldata.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/socket.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_zone.h>

#include <sys/file2.h>
#include <sys/signal2.h>

#include <machine/cpufunc.h>

/*
 * interfaces to the outside world
 */
static int pipe_read (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags);
static int pipe_write (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags);
static int pipe_close (struct file *fp);
static int pipe_shutdown (struct file *fp, int how);
static int pipe_kqfilter (struct file *fp, struct knote *kn);
static int pipe_stat (struct file *fp, struct stat *sb, struct ucred *cred);
static int pipe_ioctl (struct file *fp, u_long cmd, caddr_t data,
		struct ucred *cred, struct sysmsg *msg);

static struct fileops pipeops = {
	.fo_read = pipe_read, 
	.fo_write = pipe_write,
	.fo_ioctl = pipe_ioctl,
	.fo_kqfilter = pipe_kqfilter,
	.fo_stat = pipe_stat,
	.fo_close = pipe_close,
	.fo_shutdown = pipe_shutdown
};

static void	filt_pipedetach(struct knote *kn);
static int	filt_piperead(struct knote *kn, long hint);
static int	filt_pipewrite(struct knote *kn, long hint);

static struct filterops pipe_rfiltops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_pipedetach, filt_piperead };
static struct filterops pipe_wfiltops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_pipedetach, filt_pipewrite };

MALLOC_DEFINE(M_PIPE, "pipe", "pipe structures");

/*
 * Default pipe buffer size(s), this can be kind-of large now because pipe
 * space is pageable.  The pipe code will try to maintain locality of
 * reference for performance reasons, so small amounts of outstanding I/O
 * will not wipe the cache.
 */
#define MINPIPESIZE (PIPE_SIZE/3)
#define MAXPIPESIZE (2*PIPE_SIZE/3)

/*
 * Limit the number of "big" pipes
 */
#define LIMITBIGPIPES	64
#define PIPEQ_MAX_CACHE 16      /* per-cpu pipe structure cache */

static int pipe_maxbig = LIMITBIGPIPES;
static int pipe_maxcache = PIPEQ_MAX_CACHE;
static int pipe_bigcount;
static int pipe_nbig;
static int pipe_bcache_alloc;
static int pipe_bkmem_alloc;
static int pipe_rblocked_count;
static int pipe_wblocked_count;

SYSCTL_NODE(_kern, OID_AUTO, pipe, CTLFLAG_RW, 0, "Pipe operation");
SYSCTL_INT(_kern_pipe, OID_AUTO, nbig,
        CTLFLAG_RD, &pipe_nbig, 0, "number of big pipes allocated");
SYSCTL_INT(_kern_pipe, OID_AUTO, bigcount,
        CTLFLAG_RW, &pipe_bigcount, 0, "number of times pipe expanded");
SYSCTL_INT(_kern_pipe, OID_AUTO, rblocked,
        CTLFLAG_RW, &pipe_rblocked_count, 0, "number of times pipe expanded");
SYSCTL_INT(_kern_pipe, OID_AUTO, wblocked,
        CTLFLAG_RW, &pipe_wblocked_count, 0, "number of times pipe expanded");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxcache,
        CTLFLAG_RW, &pipe_maxcache, 0, "max pipes cached per-cpu");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxbig,
        CTLFLAG_RW, &pipe_maxbig, 0, "max number of big pipes");
static int pipe_delay = 5000;	/* 5uS default */
SYSCTL_INT(_kern_pipe, OID_AUTO, delay,
        CTLFLAG_RW, &pipe_delay, 0, "SMP delay optimization in ns");
#if !defined(NO_PIPE_SYSCTL_STATS)
SYSCTL_INT(_kern_pipe, OID_AUTO, bcache_alloc,
        CTLFLAG_RW, &pipe_bcache_alloc, 0, "pipe buffer from pcpu cache");
SYSCTL_INT(_kern_pipe, OID_AUTO, bkmem_alloc,
        CTLFLAG_RW, &pipe_bkmem_alloc, 0, "pipe buffer from kmem");
#endif

/*
 * Auto-size pipe cache to reduce kmem allocations and frees.
 */
static
void
pipeinit(void *dummy)
{
	size_t mbytes = kmem_lim_size();

	if (pipe_maxbig == LIMITBIGPIPES) {
		if (mbytes >= 7 * 1024)
			pipe_maxbig *= 2;
		if (mbytes >= 15 * 1024)
			pipe_maxbig *= 2;
	}
	if (pipe_maxcache == PIPEQ_MAX_CACHE) {
		if (mbytes >= 7 * 1024)
			pipe_maxcache *= 2;
		if (mbytes >= 15 * 1024)
			pipe_maxcache *= 2;
	}
}
SYSINIT(kmem, SI_BOOT2_MACHDEP, SI_ORDER_ANY, pipeinit, NULL)

static void pipeclose (struct pipe *cpipe);
static void pipe_free_kmem (struct pipe *cpipe);
static int pipe_create (struct pipe **cpipep);
static int pipespace (struct pipe *cpipe, int size);

static __inline void
pipewakeup(struct pipe *cpipe, int dosigio)
{
	if (dosigio && (cpipe->pipe_state & PIPE_ASYNC) && cpipe->pipe_sigio) {
		lwkt_gettoken(&sigio_token);
		pgsigio(cpipe->pipe_sigio, SIGIO, 0);
		lwkt_reltoken(&sigio_token);
	}
	KNOTE(&cpipe->pipe_kq.ki_note, 0);
}

/*
 * These routines are called before and after a UIO.  The UIO
 * may block, causing our held tokens to be lost temporarily.
 *
 * We use these routines to serialize reads against other reads
 * and writes against other writes.
 *
 * The read token is held on entry so *ipp does not race.
 */
static __inline int
pipe_start_uio(struct pipe *cpipe, int *ipp)
{
	int error;

	while (*ipp) {
		*ipp = -1;
		error = tsleep(ipp, PCATCH, "pipexx", 0);
		if (error)
			return (error);
	}
	*ipp = 1;
	return (0);
}

static __inline void
pipe_end_uio(struct pipe *cpipe, int *ipp)
{
	if (*ipp < 0) {
		*ipp = 0;
		wakeup(ipp);
	} else {
		KKASSERT(*ipp > 0);
		*ipp = 0;
	}
}

/*
 * The pipe system call for the DTYPE_PIPE type of pipes
 *
 * pipe_args(int dummy)
 *
 * MPSAFE
 */
int
sys_pipe(struct pipe_args *uap)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *rf, *wf;
	struct pipe *rpipe, *wpipe;
	int fd1, fd2, error;

	rpipe = wpipe = NULL;
	if (pipe_create(&rpipe) || pipe_create(&wpipe)) {
		pipeclose(rpipe); 
		pipeclose(wpipe); 
		return (ENFILE);
	}
	
	error = falloc(td->td_lwp, &rf, &fd1);
	if (error) {
		pipeclose(rpipe);
		pipeclose(wpipe);
		return (error);
	}
	uap->sysmsg_fds[0] = fd1;

	/*
	 * Warning: once we've gotten past allocation of the fd for the
	 * read-side, we can only drop the read side via fdrop() in order
	 * to avoid races against processes which manage to dup() the read
	 * side while we are blocked trying to allocate the write side.
	 */
	rf->f_type = DTYPE_PIPE;
	rf->f_flag = FREAD | FWRITE;
	rf->f_ops = &pipeops;
	rf->f_data = rpipe;
	error = falloc(td->td_lwp, &wf, &fd2);
	if (error) {
		fsetfd(fdp, NULL, fd1);
		fdrop(rf);
		/* rpipe has been closed by fdrop(). */
		pipeclose(wpipe);
		return (error);
	}
	wf->f_type = DTYPE_PIPE;
	wf->f_flag = FREAD | FWRITE;
	wf->f_ops = &pipeops;
	wf->f_data = wpipe;
	uap->sysmsg_fds[1] = fd2;

	rpipe->pipe_slock = kmalloc(sizeof(struct lock),
				    M_PIPE, M_WAITOK|M_ZERO);
	wpipe->pipe_slock = rpipe->pipe_slock;
	rpipe->pipe_peer = wpipe;
	wpipe->pipe_peer = rpipe;
	lockinit(rpipe->pipe_slock, "pipecl", 0, 0);

	/*
	 * Once activated the peer relationship remains valid until
	 * both sides are closed.
	 */
	fsetfd(fdp, rf, fd1);
	fsetfd(fdp, wf, fd2);
	fdrop(rf);
	fdrop(wf);

	return (0);
}

/*
 * Allocate kva for pipe circular buffer, the space is pageable
 * This routine will 'realloc' the size of a pipe safely, if it fails
 * it will retain the old buffer.
 * If it fails it will return ENOMEM.
 */
static int
pipespace(struct pipe *cpipe, int size)
{
	struct vm_object *object;
	caddr_t buffer;
	int npages, error;

	npages = round_page(size) / PAGE_SIZE;
	object = cpipe->pipe_buffer.object;

	/*
	 * [re]create the object if necessary and reserve space for it
	 * in the kernel_map.  The object and memory are pageable.  On
	 * success, free the old resources before assigning the new
	 * ones.
	 */
	if (object == NULL || object->size != npages) {
		object = vm_object_allocate(OBJT_DEFAULT, npages);
		buffer = (caddr_t)vm_map_min(&kernel_map);

		error = vm_map_find(&kernel_map, object, NULL,
				    0, (vm_offset_t *)&buffer, size,
				    PAGE_SIZE,
				    1, VM_MAPTYPE_NORMAL,
				    VM_PROT_ALL, VM_PROT_ALL, 0);

		if (error != KERN_SUCCESS) {
			vm_object_deallocate(object);
			return (ENOMEM);
		}
		pipe_free_kmem(cpipe);
		cpipe->pipe_buffer.object = object;
		cpipe->pipe_buffer.buffer = buffer;
		cpipe->pipe_buffer.size = size;
		++pipe_bkmem_alloc;
	} else {
		++pipe_bcache_alloc;
	}
	cpipe->pipe_buffer.rindex = 0;
	cpipe->pipe_buffer.windex = 0;
	return (0);
}

/*
 * Initialize and allocate VM and memory for pipe, pulling the pipe from
 * our per-cpu cache if possible.  For now make sure it is sized for the
 * smaller PIPE_SIZE default.
 */
static int
pipe_create(struct pipe **cpipep)
{
	globaldata_t gd = mycpu;
	struct pipe *cpipe;
	int error;

	if ((cpipe = gd->gd_pipeq) != NULL) {
		gd->gd_pipeq = cpipe->pipe_peer;
		--gd->gd_pipeqcount;
		cpipe->pipe_peer = NULL;
		cpipe->pipe_wantwcnt = 0;
	} else {
		cpipe = kmalloc(sizeof(struct pipe), M_PIPE, M_WAITOK|M_ZERO);
	}
	*cpipep = cpipe;
	if ((error = pipespace(cpipe, PIPE_SIZE)) != 0)
		return (error);
	vfs_timestamp(&cpipe->pipe_ctime);
	cpipe->pipe_atime = cpipe->pipe_ctime;
	cpipe->pipe_mtime = cpipe->pipe_ctime;
	lwkt_token_init(&cpipe->pipe_rlock, "piper");
	lwkt_token_init(&cpipe->pipe_wlock, "pipew");
	return (0);
}

static int
pipe_read(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct pipe *rpipe;
	struct pipe *wpipe;
	int error;
	size_t nread = 0;
	int nbio;
	u_int size;	/* total bytes available */
	u_int nsize;	/* total bytes to read */
	u_int rindex;	/* contiguous bytes available */
	int notify_writer;
	int bigread;
	int bigcount;

	atomic_set_int(&curthread->td_mpflags, TDF_MP_BATCH_DEMARC);

	if (uio->uio_resid == 0)
		return(0);

	/*
	 * Setup locks, calculate nbio
	 */
	rpipe = (struct pipe *)fp->f_data;
	wpipe = rpipe->pipe_peer;
	lwkt_gettoken(&rpipe->pipe_rlock);

	if (fflags & O_FBLOCKING)
		nbio = 0;
	else if (fflags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * Reads are serialized.  Note however that pipe_buffer.buffer and
	 * pipe_buffer.size can change out from under us when the number
	 * of bytes in the buffer are zero due to the write-side doing a
	 * pipespace().
	 */
	error = pipe_start_uio(rpipe, &rpipe->pipe_rip);
	if (error) {
		lwkt_reltoken(&rpipe->pipe_rlock);
		return (error);
	}
	notify_writer = 0;

	bigread = (uio->uio_resid > 10 * 1024 * 1024);
	bigcount = 10;

	while (uio->uio_resid) {
		/*
		 * Don't hog the cpu.
		 */
		if (bigread && --bigcount == 0) {
			lwkt_user_yield();
			bigcount = 10;
			if (CURSIG(curthread->td_lwp)) {
				error = EINTR;
				break;
			}
		}

		size = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;
		cpu_lfence();
		if (size) {
			rindex = rpipe->pipe_buffer.rindex &
				 (rpipe->pipe_buffer.size - 1);
			nsize = size;
			if (nsize > rpipe->pipe_buffer.size - rindex)
				nsize = rpipe->pipe_buffer.size - rindex;
			nsize = szmin(nsize, uio->uio_resid);

			error = uiomove(&rpipe->pipe_buffer.buffer[rindex],
					nsize, uio);
			if (error)
				break;
			cpu_mfence();
			rpipe->pipe_buffer.rindex += nsize;
			nread += nsize;

			/*
			 * If the FIFO is still over half full just continue
			 * and do not try to notify the writer yet.
			 */
			if (size - nsize >= (rpipe->pipe_buffer.size >> 1)) {
				notify_writer = 0;
				continue;
			}

			/*
			 * When the FIFO is less then half full notify any
			 * waiting writer.  WANTW can be checked while
			 * holding just the rlock.
			 */
			notify_writer = 1;
			if ((rpipe->pipe_state & PIPE_WANTW) == 0)
				continue;
		}

		/*
		 * If the "write-side" was blocked we wake it up.  This code
		 * is reached either when the buffer is completely emptied
		 * or if it becomes more then half-empty.
		 *
		 * Pipe_state can only be modified if both the rlock and
		 * wlock are held.
		 */
		if (rpipe->pipe_state & PIPE_WANTW) {
			lwkt_gettoken(&rpipe->pipe_wlock);
			if (rpipe->pipe_state & PIPE_WANTW) {
				rpipe->pipe_state &= ~PIPE_WANTW;
				lwkt_reltoken(&rpipe->pipe_wlock);
				wakeup(rpipe);
			} else {
				lwkt_reltoken(&rpipe->pipe_wlock);
			}
		}

		/*
		 * Pick up our copy loop again if the writer sent data to
		 * us while we were messing around.
		 *
		 * On a SMP box poll up to pipe_delay nanoseconds for new
		 * data.  Typically a value of 2000 to 4000 is sufficient
		 * to eradicate most IPIs/tsleeps/wakeups when a pipe
		 * is used for synchronous communications with small packets,
		 * and 8000 or so (8uS) will pipeline large buffer xfers
		 * between cpus over a pipe.
		 *
		 * For synchronous communications a hit means doing a
		 * full Awrite-Bread-Bwrite-Aread cycle in less then 2uS,
		 * where as miss requiring a tsleep/wakeup sequence
		 * will take 7uS or more.
		 */
		if (rpipe->pipe_buffer.windex != rpipe->pipe_buffer.rindex)
			continue;

#ifdef _RDTSC_SUPPORTED_
		if (pipe_delay) {
			int64_t tsc_target;
			int good = 0;

			tsc_target = tsc_get_target(pipe_delay);
			while (tsc_test_target(tsc_target) == 0) {
				if (rpipe->pipe_buffer.windex !=
				    rpipe->pipe_buffer.rindex) {
					good = 1;
					break;
				}
			}
			if (good)
				continue;
		}
#endif

		/*
		 * Detect EOF condition, do not set error.
		 */
		if (rpipe->pipe_state & PIPE_REOF)
			break;

		/*
		 * Break if some data was read, or if this was a non-blocking
		 * read.
		 */
		if (nread > 0)
			break;

		if (nbio) {
			error = EAGAIN;
			break;
		}

		/*
		 * Last chance, interlock with WANTR.
		 */
		lwkt_gettoken(&rpipe->pipe_wlock);
		size = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;
		if (size) {
			lwkt_reltoken(&rpipe->pipe_wlock);
			continue;
		}

		/*
		 * Retest EOF - acquiring a new token can temporarily release
		 * tokens already held.
		 */
		if (rpipe->pipe_state & PIPE_REOF) {
			lwkt_reltoken(&rpipe->pipe_wlock);
			break;
		}

		/*
		 * If there is no more to read in the pipe, reset its
		 * pointers to the beginning.  This improves cache hit
		 * stats.
		 *
		 * We need both locks to modify both pointers, and there
		 * must also not be a write in progress or the uiomove()
		 * in the write might block and temporarily release
		 * its wlock, then reacquire and update windex.  We are
		 * only serialized against reads, not writes.
		 *
		 * XXX should we even bother resetting the indices?  It
		 *     might actually be more cache efficient not to.
		 */
		if (rpipe->pipe_buffer.rindex == rpipe->pipe_buffer.windex &&
		    rpipe->pipe_wip == 0) {
			rpipe->pipe_buffer.rindex = 0;
			rpipe->pipe_buffer.windex = 0;
		}

		/*
		 * Wait for more data.
		 *
		 * Pipe_state can only be set if both the rlock and wlock
		 * are held.
		 */
		rpipe->pipe_state |= PIPE_WANTR;
		tsleep_interlock(rpipe, PCATCH);
		lwkt_reltoken(&rpipe->pipe_wlock);
		error = tsleep(rpipe, PCATCH | PINTERLOCKED, "piperd", 0);
		++pipe_rblocked_count;
		if (error)
			break;
	}
	pipe_end_uio(rpipe, &rpipe->pipe_rip);

	/*
	 * Uptime last access time
	 */
	if (error == 0 && nread)
		vfs_timestamp(&rpipe->pipe_atime);

	/*
	 * If we drained the FIFO more then half way then handle
	 * write blocking hysteresis.
	 *
	 * Note that PIPE_WANTW cannot be set by the writer without
	 * it holding both rlock and wlock, so we can test it
	 * while holding just rlock.
	 */
	if (notify_writer) {
		/*
		 * Synchronous blocking is done on the pipe involved
		 */
		if (rpipe->pipe_state & PIPE_WANTW) {
			lwkt_gettoken(&rpipe->pipe_wlock);
			if (rpipe->pipe_state & PIPE_WANTW) {
				rpipe->pipe_state &= ~PIPE_WANTW;
				lwkt_reltoken(&rpipe->pipe_wlock);
				wakeup(rpipe);
			} else {
				lwkt_reltoken(&rpipe->pipe_wlock);
			}
		}

		/*
		 * But we may also have to deal with a kqueue which is
		 * stored on the same pipe as its descriptor, so a
		 * EVFILT_WRITE event waiting for our side to drain will
		 * be on the other side.
		 */
		lwkt_gettoken(&wpipe->pipe_wlock);
		pipewakeup(wpipe, 0);
		lwkt_reltoken(&wpipe->pipe_wlock);
	}
	/*size = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;*/
	lwkt_reltoken(&rpipe->pipe_rlock);

	return (error);
}

static int
pipe_write(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	int error;
	int orig_resid;
	int nbio;
	struct pipe *wpipe;
	struct pipe *rpipe;
	u_int windex;
	u_int space;
	u_int wcount;
	int bigwrite;
	int bigcount;

	/*
	 * Writes go to the peer.  The peer will always exist.
	 */
	rpipe = (struct pipe *) fp->f_data;
	wpipe = rpipe->pipe_peer;
	lwkt_gettoken(&wpipe->pipe_wlock);
	if (wpipe->pipe_state & PIPE_WEOF) {
		lwkt_reltoken(&wpipe->pipe_wlock);
		return (EPIPE);
	}

	/*
	 * Degenerate case (EPIPE takes prec)
	 */
	if (uio->uio_resid == 0) {
		lwkt_reltoken(&wpipe->pipe_wlock);
		return(0);
	}

	/*
	 * Writes are serialized (start_uio must be called with wlock)
	 */
	error = pipe_start_uio(wpipe, &wpipe->pipe_wip);
	if (error) {
		lwkt_reltoken(&wpipe->pipe_wlock);
		return (error);
	}

	if (fflags & O_FBLOCKING)
		nbio = 0;
	else if (fflags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * If it is advantageous to resize the pipe buffer, do
	 * so.  We are write-serialized so we can block safely.
	 */
	if ((wpipe->pipe_buffer.size <= PIPE_SIZE) &&
	    (pipe_nbig < pipe_maxbig) &&
	    wpipe->pipe_wantwcnt > 4 &&
	    (wpipe->pipe_buffer.rindex == wpipe->pipe_buffer.windex)) {
		/* 
		 * Recheck after lock.
		 */
		lwkt_gettoken(&wpipe->pipe_rlock);
		if ((wpipe->pipe_buffer.size <= PIPE_SIZE) &&
		    (pipe_nbig < pipe_maxbig) &&
		    (wpipe->pipe_buffer.rindex == wpipe->pipe_buffer.windex)) {
			atomic_add_int(&pipe_nbig, 1);
			if (pipespace(wpipe, BIG_PIPE_SIZE) == 0)
				++pipe_bigcount;
			else
				atomic_subtract_int(&pipe_nbig, 1);
		}
		lwkt_reltoken(&wpipe->pipe_rlock);
	}

	orig_resid = uio->uio_resid;
	wcount = 0;

	bigwrite = (uio->uio_resid > 10 * 1024 * 1024);
	bigcount = 10;

	while (uio->uio_resid) {
		if (wpipe->pipe_state & PIPE_WEOF) {
			error = EPIPE;
			break;
		}

		/*
		 * Don't hog the cpu.
		 */
		if (bigwrite && --bigcount == 0) {
			lwkt_user_yield();
			bigcount = 10;
			if (CURSIG(curthread->td_lwp)) {
				error = EINTR;
				break;
			}
		}

		windex = wpipe->pipe_buffer.windex &
			 (wpipe->pipe_buffer.size - 1);
		space = wpipe->pipe_buffer.size -
			(wpipe->pipe_buffer.windex - wpipe->pipe_buffer.rindex);
		cpu_lfence();

		/* Writes of size <= PIPE_BUF must be atomic. */
		if ((space < uio->uio_resid) && (orig_resid <= PIPE_BUF))
			space = 0;

		/* 
		 * Write to fill, read size handles write hysteresis.  Also
		 * additional restrictions can cause select-based non-blocking
		 * writes to spin.
		 */
		if (space > 0) {
			u_int segsize;

			/*
			 * Transfer size is minimum of uio transfer
			 * and free space in pipe buffer.
			 *
			 * Limit each uiocopy to no more then PIPE_SIZE
			 * so we can keep the gravy train going on a
			 * SMP box.  This doubles the performance for
			 * write sizes > 16K.  Otherwise large writes
			 * wind up doing an inefficient synchronous
			 * ping-pong.
			 */
			space = szmin(space, uio->uio_resid);
			if (space > PIPE_SIZE)
				space = PIPE_SIZE;

			/*
			 * First segment to transfer is minimum of
			 * transfer size and contiguous space in
			 * pipe buffer.  If first segment to transfer
			 * is less than the transfer size, we've got
			 * a wraparound in the buffer.
			 */
			segsize = wpipe->pipe_buffer.size - windex;
			if (segsize > space)
				segsize = space;

			/*
			 * If this is the first loop and the reader is
			 * blocked, do a preemptive wakeup of the reader.
			 *
			 * On SMP the IPI latency plus the wlock interlock
			 * on the reader side is the fastest way to get the
			 * reader going.  (The scheduler will hard loop on
			 * lock tokens).
			 *
			 * NOTE: We can't clear WANTR here without acquiring
			 * the rlock, which we don't want to do here!
			 */
			if ((wpipe->pipe_state & PIPE_WANTR))
				wakeup(wpipe);

			/*
			 * Transfer segment, which may include a wrap-around.
			 * Update windex to account for both all in one go
			 * so the reader can read() the data atomically.
			 */
			error = uiomove(&wpipe->pipe_buffer.buffer[windex],
					segsize, uio);
			if (error == 0 && segsize < space) {
				segsize = space - segsize;
				error = uiomove(&wpipe->pipe_buffer.buffer[0],
						segsize, uio);
			}
			if (error)
				break;
			cpu_mfence();
			wpipe->pipe_buffer.windex += space;
			wcount += space;
			continue;
		}

		/*
		 * We need both the rlock and the wlock to interlock against
		 * the EOF, WANTW, and size checks, and to modify pipe_state.
		 *
		 * These are token locks so we do not have to worry about
		 * deadlocks.
		 */
		lwkt_gettoken(&wpipe->pipe_rlock);

		/*
		 * If the "read-side" has been blocked, wake it up now
		 * and yield to let it drain synchronously rather
		 * then block.
		 */
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}

		/*
		 * don't block on non-blocking I/O
		 */
		if (nbio) {
			lwkt_reltoken(&wpipe->pipe_rlock);
			error = EAGAIN;
			break;
		}

		/*
		 * re-test whether we have to block in the writer after
		 * acquiring both locks, in case the reader opened up
		 * some space.
		 */
		space = wpipe->pipe_buffer.size -
			(wpipe->pipe_buffer.windex - wpipe->pipe_buffer.rindex);
		cpu_lfence();
		if ((space < uio->uio_resid) && (orig_resid <= PIPE_BUF))
			space = 0;

		/*
		 * Retest EOF - acquiring a new token can temporarily release
		 * tokens already held.
		 */
		if (wpipe->pipe_state & PIPE_WEOF) {
			lwkt_reltoken(&wpipe->pipe_rlock);
			error = EPIPE;
			break;
		}

		/*
		 * We have no more space and have something to offer,
		 * wake up select/poll/kq.
		 */
		if (space == 0) {
			wpipe->pipe_state |= PIPE_WANTW;
			++wpipe->pipe_wantwcnt;
			pipewakeup(wpipe, 1);
			if (wpipe->pipe_state & PIPE_WANTW)
				error = tsleep(wpipe, PCATCH, "pipewr", 0);
			++pipe_wblocked_count;
		}
		lwkt_reltoken(&wpipe->pipe_rlock);

		/*
		 * Break out if we errored or the read side wants us to go
		 * away.
		 */
		if (error)
			break;
		if (wpipe->pipe_state & PIPE_WEOF) {
			error = EPIPE;
			break;
		}
	}
	pipe_end_uio(wpipe, &wpipe->pipe_wip);

	/*
	 * If we have put any characters in the buffer, we wake up
	 * the reader.
	 *
	 * Both rlock and wlock are required to be able to modify pipe_state.
	 */
	if (wpipe->pipe_buffer.windex != wpipe->pipe_buffer.rindex) {
		if (wpipe->pipe_state & PIPE_WANTR) {
			lwkt_gettoken(&wpipe->pipe_rlock);
			if (wpipe->pipe_state & PIPE_WANTR) {
				wpipe->pipe_state &= ~PIPE_WANTR;
				lwkt_reltoken(&wpipe->pipe_rlock);
				wakeup(wpipe);
			} else {
				lwkt_reltoken(&wpipe->pipe_rlock);
			}
		}
		lwkt_gettoken(&wpipe->pipe_rlock);
		pipewakeup(wpipe, 1);
		lwkt_reltoken(&wpipe->pipe_rlock);
	}

	/*
	 * Don't return EPIPE if I/O was successful
	 */
	if ((wpipe->pipe_buffer.rindex == wpipe->pipe_buffer.windex) &&
	    (uio->uio_resid == 0) &&
	    (error == EPIPE)) {
		error = 0;
	}

	if (error == 0)
		vfs_timestamp(&wpipe->pipe_mtime);

	/*
	 * We have something to offer,
	 * wake up select/poll/kq.
	 */
	/*space = wpipe->pipe_buffer.windex - wpipe->pipe_buffer.rindex;*/
	lwkt_reltoken(&wpipe->pipe_wlock);
	return (error);
}

/*
 * we implement a very minimal set of ioctls for compatibility with sockets.
 */
static int
pipe_ioctl(struct file *fp, u_long cmd, caddr_t data,
	   struct ucred *cred, struct sysmsg *msg)
{
	struct pipe *mpipe;
	int error;

	mpipe = (struct pipe *)fp->f_data;

	lwkt_gettoken(&mpipe->pipe_rlock);
	lwkt_gettoken(&mpipe->pipe_wlock);

	switch (cmd) {
	case FIOASYNC:
		if (*(int *)data) {
			mpipe->pipe_state |= PIPE_ASYNC;
		} else {
			mpipe->pipe_state &= ~PIPE_ASYNC;
		}
		error = 0;
		break;
	case FIONREAD:
		*(int *)data = mpipe->pipe_buffer.windex -
				mpipe->pipe_buffer.rindex;
		error = 0;
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)data, &mpipe->pipe_sigio);
		break;
	case FIOGETOWN:
		*(int *)data = fgetown(&mpipe->pipe_sigio);
		error = 0;
		break;
	case TIOCSPGRP:
		/* This is deprecated, FIOSETOWN should be used instead. */
		error = fsetown(-(*(int *)data), &mpipe->pipe_sigio);
		break;

	case TIOCGPGRP:
		/* This is deprecated, FIOGETOWN should be used instead. */
		*(int *)data = -fgetown(&mpipe->pipe_sigio);
		error = 0;
		break;
	default:
		error = ENOTTY;
		break;
	}
	lwkt_reltoken(&mpipe->pipe_wlock);
	lwkt_reltoken(&mpipe->pipe_rlock);

	return (error);
}

/*
 * MPSAFE
 */
static int
pipe_stat(struct file *fp, struct stat *ub, struct ucred *cred)
{
	struct pipe *pipe;

	pipe = (struct pipe *)fp->f_data;

	bzero((caddr_t)ub, sizeof(*ub));
	ub->st_mode = S_IFIFO;
	ub->st_blksize = pipe->pipe_buffer.size;
	ub->st_size = pipe->pipe_buffer.windex - pipe->pipe_buffer.rindex;
	ub->st_blocks = (ub->st_size + ub->st_blksize - 1) / ub->st_blksize;
	ub->st_atimespec = pipe->pipe_atime;
	ub->st_mtimespec = pipe->pipe_mtime;
	ub->st_ctimespec = pipe->pipe_ctime;
	/*
	 * Left as 0: st_dev, st_ino, st_nlink, st_uid, st_gid, st_rdev,
	 * st_flags, st_gen.
	 * XXX (st_dev, st_ino) should be unique.
	 */
	return (0);
}

static int
pipe_close(struct file *fp)
{
	struct pipe *cpipe;

	cpipe = (struct pipe *)fp->f_data;
	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	funsetown(&cpipe->pipe_sigio);
	pipeclose(cpipe);
	return (0);
}

/*
 * Shutdown one or both directions of a full-duplex pipe.
 */
static int
pipe_shutdown(struct file *fp, int how)
{
	struct pipe *rpipe;
	struct pipe *wpipe;
	int error = EPIPE;

	rpipe = (struct pipe *)fp->f_data;
	wpipe = rpipe->pipe_peer;

	/*
	 * We modify pipe_state on both pipes, which means we need
	 * all four tokens!
	 */
	lwkt_gettoken(&rpipe->pipe_rlock);
	lwkt_gettoken(&rpipe->pipe_wlock);
	lwkt_gettoken(&wpipe->pipe_rlock);
	lwkt_gettoken(&wpipe->pipe_wlock);

	switch(how) {
	case SHUT_RDWR:
	case SHUT_RD:
		rpipe->pipe_state |= PIPE_REOF;		/* my reads */
		rpipe->pipe_state |= PIPE_WEOF;		/* peer writes */
		if (rpipe->pipe_state & PIPE_WANTR) {
			rpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(rpipe);
		}
		if (rpipe->pipe_state & PIPE_WANTW) {
			rpipe->pipe_state &= ~PIPE_WANTW;
			wakeup(rpipe);
		}
		error = 0;
		if (how == SHUT_RD)
			break;
		/* fall through */
	case SHUT_WR:
		wpipe->pipe_state |= PIPE_REOF;		/* peer reads */
		wpipe->pipe_state |= PIPE_WEOF;		/* my writes */
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}
		if (wpipe->pipe_state & PIPE_WANTW) {
			wpipe->pipe_state &= ~PIPE_WANTW;
			wakeup(wpipe);
		}
		error = 0;
		break;
	}
	pipewakeup(rpipe, 1);
	pipewakeup(wpipe, 1);

	lwkt_reltoken(&wpipe->pipe_wlock);
	lwkt_reltoken(&wpipe->pipe_rlock);
	lwkt_reltoken(&rpipe->pipe_wlock);
	lwkt_reltoken(&rpipe->pipe_rlock);

	return (error);
}

static void
pipe_free_kmem(struct pipe *cpipe)
{
	if (cpipe->pipe_buffer.buffer != NULL) {
		if (cpipe->pipe_buffer.size > PIPE_SIZE)
			atomic_subtract_int(&pipe_nbig, 1);
		kmem_free(&kernel_map,
			(vm_offset_t)cpipe->pipe_buffer.buffer,
			cpipe->pipe_buffer.size);
		cpipe->pipe_buffer.buffer = NULL;
		cpipe->pipe_buffer.object = NULL;
	}
}

/*
 * Close the pipe.  The slock must be held to interlock against simultanious
 * closes.  The rlock and wlock must be held to adjust the pipe_state.
 */
static void
pipeclose(struct pipe *cpipe)
{
	globaldata_t gd;
	struct pipe *ppipe;

	if (cpipe == NULL)
		return;

	/*
	 * The slock may not have been allocated yet (close during
	 * initialization)
	 *
	 * We need both the read and write tokens to modify pipe_state.
	 */
	if (cpipe->pipe_slock)
		lockmgr(cpipe->pipe_slock, LK_EXCLUSIVE);
	lwkt_gettoken(&cpipe->pipe_rlock);
	lwkt_gettoken(&cpipe->pipe_wlock);

	/*
	 * Set our state, wakeup anyone waiting in select/poll/kq, and
	 * wakeup anyone blocked on our pipe.
	 */
	cpipe->pipe_state |= PIPE_CLOSED | PIPE_REOF | PIPE_WEOF;
	pipewakeup(cpipe, 1);
	if (cpipe->pipe_state & (PIPE_WANTR | PIPE_WANTW)) {
		cpipe->pipe_state &= ~(PIPE_WANTR | PIPE_WANTW);
		wakeup(cpipe);
	}

	/*
	 * Disconnect from peer.
	 */
	if ((ppipe = cpipe->pipe_peer) != NULL) {
		lwkt_gettoken(&ppipe->pipe_rlock);
		lwkt_gettoken(&ppipe->pipe_wlock);
		ppipe->pipe_state |= PIPE_REOF | PIPE_WEOF;
		pipewakeup(ppipe, 1);
		if (ppipe->pipe_state & (PIPE_WANTR | PIPE_WANTW)) {
			ppipe->pipe_state &= ~(PIPE_WANTR | PIPE_WANTW);
			wakeup(ppipe);
		}
		if (SLIST_FIRST(&ppipe->pipe_kq.ki_note))
			KNOTE(&ppipe->pipe_kq.ki_note, 0);
		lwkt_reltoken(&ppipe->pipe_wlock);
		lwkt_reltoken(&ppipe->pipe_rlock);
	}

	/*
	 * If the peer is also closed we can free resources for both
	 * sides, otherwise we leave our side intact to deal with any
	 * races (since we only have the slock).
	 */
	if (ppipe && (ppipe->pipe_state & PIPE_CLOSED)) {
		cpipe->pipe_peer = NULL;
		ppipe->pipe_peer = NULL;
		ppipe->pipe_slock = NULL;	/* we will free the slock */
		pipeclose(ppipe);
		ppipe = NULL;
	}

	lwkt_reltoken(&cpipe->pipe_wlock);
	lwkt_reltoken(&cpipe->pipe_rlock);
	if (cpipe->pipe_slock)
		lockmgr(cpipe->pipe_slock, LK_RELEASE);

	/*
	 * If we disassociated from our peer we can free resources
	 */
	if (ppipe == NULL) {
		gd = mycpu;
		if (cpipe->pipe_slock) {
			kfree(cpipe->pipe_slock, M_PIPE);
			cpipe->pipe_slock = NULL;
		}
		if (gd->gd_pipeqcount >= pipe_maxcache ||
		    cpipe->pipe_buffer.size != PIPE_SIZE
		) {
			pipe_free_kmem(cpipe);
			kfree(cpipe, M_PIPE);
		} else {
			cpipe->pipe_state = 0;
			cpipe->pipe_peer = gd->gd_pipeq;
			gd->gd_pipeq = cpipe;
			++gd->gd_pipeqcount;
		}
	}
}

static int
pipe_kqfilter(struct file *fp, struct knote *kn)
{
	struct pipe *cpipe;

	cpipe = (struct pipe *)kn->kn_fp->f_data;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &pipe_rfiltops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &pipe_wfiltops;
		if (cpipe->pipe_peer == NULL) {
			/* other end of pipe has been closed */
			return (EPIPE);
		}
		break;
	default:
		return (EOPNOTSUPP);
	}
	kn->kn_hook = (caddr_t)cpipe;

	knote_insert(&cpipe->pipe_kq.ki_note, kn);

	return (0);
}

static void
filt_pipedetach(struct knote *kn)
{
	struct pipe *cpipe = (struct pipe *)kn->kn_hook;

	knote_remove(&cpipe->pipe_kq.ki_note, kn);
}

/*ARGSUSED*/
static int
filt_piperead(struct knote *kn, long hint)
{
	struct pipe *rpipe = (struct pipe *)kn->kn_fp->f_data;
	int ready = 0;

	lwkt_gettoken(&rpipe->pipe_rlock);
	lwkt_gettoken(&rpipe->pipe_wlock);

	kn->kn_data = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;

	if (rpipe->pipe_state & PIPE_REOF) {
		/*
		 * Only set NODATA if all data has been exhausted
		 */
		if (kn->kn_data == 0)
			kn->kn_flags |= EV_NODATA;
		kn->kn_flags |= EV_EOF; 
		ready = 1;
	}

	lwkt_reltoken(&rpipe->pipe_wlock);
	lwkt_reltoken(&rpipe->pipe_rlock);

	if (!ready)
		ready = kn->kn_data > 0;

	return (ready);
}

/*ARGSUSED*/
static int
filt_pipewrite(struct knote *kn, long hint)
{
	struct pipe *rpipe = (struct pipe *)kn->kn_fp->f_data;
	struct pipe *wpipe = rpipe->pipe_peer;
	int ready = 0;

	kn->kn_data = 0;
	if (wpipe == NULL) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}

	lwkt_gettoken(&wpipe->pipe_rlock);
	lwkt_gettoken(&wpipe->pipe_wlock);

	if (wpipe->pipe_state & PIPE_WEOF) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		ready = 1;
	}

	if (!ready)
		kn->kn_data = wpipe->pipe_buffer.size -
			      (wpipe->pipe_buffer.windex -
			       wpipe->pipe_buffer.rindex);

	lwkt_reltoken(&wpipe->pipe_wlock);
	lwkt_reltoken(&wpipe->pipe_rlock);

	if (!ready)
		ready = kn->kn_data >= PIPE_BUF;

	return (ready);
}
