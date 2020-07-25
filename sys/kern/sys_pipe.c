/*
 * Copyright (c) 1996 John S. Dyson
 * All rights reserved.
 * Copyright (c) 2003-2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
#include <sys/sysmsg.h>
#include <sys/pipe.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/event.h>
#include <sys/globaldata.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_zone.h>

#include <sys/file2.h>
#include <sys/signal2.h>
#include <sys/mutex2.h>

#include <machine/cpufunc.h>

struct pipegdlock {
	struct mtx	mtx;
} __cachealign;

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

__read_mostly static struct fileops pipeops = {
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

__read_mostly static struct filterops pipe_rfiltops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_pipedetach, filt_piperead };
__read_mostly static struct filterops pipe_wfiltops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_pipedetach, filt_pipewrite };

MALLOC_DEFINE(M_PIPE, "pipe", "pipe structures");

#define PIPEQ_MAX_CACHE 16      /* per-cpu pipe structure cache */

__read_mostly static int pipe_maxcache = PIPEQ_MAX_CACHE;
__read_mostly static struct pipegdlock *pipe_gdlocks;

SYSCTL_NODE(_kern, OID_AUTO, pipe, CTLFLAG_RW, 0, "Pipe operation");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxcache,
        CTLFLAG_RW, &pipe_maxcache, 0, "max pipes cached per-cpu");

/*
 * The pipe buffer size can be changed at any time.  Only new pipe()s
 * are affected.  Note that due to cpu cache effects, you do not want
 * to make this value too large.
 */
__read_mostly static int pipe_size = 32768;
SYSCTL_INT(_kern_pipe, OID_AUTO, size,
        CTLFLAG_RW, &pipe_size, 0, "Pipe buffer size (16384 minimum)");

/*
 * Reader/writer delay loop.  When the reader exhausts the pipe buffer
 * or the write completely fills the pipe buffer and would otherwise sleep,
 * it first busy-loops for a few microseconds waiting for data or buffer
 * space.  This eliminates IPIs for most high-bandwidth writer/reader pipes
 * and also helps when the user program uses a large data buffer in its
 * UIOs.
 *
 * This defaults to 4uS.
 */
#ifdef _RDTSC_SUPPORTED_
__read_mostly static int pipe_delay = 4000;	/* 4uS default */
SYSCTL_INT(_kern_pipe, OID_AUTO, delay,
        CTLFLAG_RW, &pipe_delay, 0, "SMP delay optimization in ns");
#endif

/*
 * Auto-size pipe cache to reduce kmem allocations and frees.
 */
static
void
pipeinit(void *dummy)
{
	size_t mbytes = kmem_lim_size();
	int n;

	if (pipe_maxcache == PIPEQ_MAX_CACHE) {
		if (mbytes >= 7 * 1024)
			pipe_maxcache *= 2;
		if (mbytes >= 15 * 1024)
			pipe_maxcache *= 2;
	}

	/*
	 * Detune the pcpu caching a bit on systems with an insane number
	 * of cpu threads to reduce memory waste.
	 */
	if (ncpus > 64) {
		pipe_maxcache = pipe_maxcache * 64 / ncpus;
		if (pipe_maxcache < PIPEQ_MAX_CACHE)
			pipe_maxcache = PIPEQ_MAX_CACHE;
	}

	pipe_gdlocks = kmalloc(sizeof(*pipe_gdlocks) * ncpus,
			     M_PIPE, M_WAITOK | M_ZERO);
	for (n = 0; n < ncpus; ++n)
		mtx_init(&pipe_gdlocks[n].mtx, "pipekm");
}
SYSINIT(kmem, SI_BOOT2_MACHDEP, SI_ORDER_ANY, pipeinit, NULL);

static void pipeclose (struct pipe *pipe,
		struct pipebuf *pbr, struct pipebuf *pbw);
static void pipe_free_kmem (struct pipebuf *buf);
static int pipe_create (struct pipe **pipep);

/*
 * Test and clear the specified flag, wakeup(pb) if it was set.
 * This function must also act as a memory barrier.
 */
static __inline void
pipesignal(struct pipebuf *pb, uint32_t flags)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = pb->state;
		cpu_ccfence();
		nflags = oflags & ~flags;
		if (atomic_cmpset_int(&pb->state, oflags, nflags))
			break;
	}
	if (oflags & flags)
		wakeup(pb);
}

/*
 *
 */
static __inline void
pipewakeup(struct pipebuf *pb, int dosigio)
{
	if (dosigio && (pb->state & PIPE_ASYNC) && pb->sigio) {
		lwkt_gettoken(&sigio_token);
		pgsigio(pb->sigio, SIGIO, 0);
		lwkt_reltoken(&sigio_token);
	}
	KNOTE(&pb->kq.ki_note, 0);
}

/*
 * These routines are called before and after a UIO.  The UIO
 * may block, causing our held tokens to be lost temporarily.
 *
 * We use these routines to serialize reads against other reads
 * and writes against other writes.
 *
 * The appropriate token is held on entry so *ipp does not race.
 */
static __inline int
pipe_start_uio(int *ipp)
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
pipe_end_uio(int *ipp)
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
sys_pipe(struct sysmsg *sysmsg, const struct pipe_args *uap)
{
	return kern_pipe(sysmsg->sysmsg_fds, 0);
}

int
sys_pipe2(struct sysmsg *sysmsg, const struct pipe2_args *uap)
{
	return kern_pipe(sysmsg->sysmsg_fds, uap->flags);
}

int
kern_pipe(long *fds, int flags)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *rf, *wf;
	struct pipe *pipe;
	int fd1, fd2, error;

	pipe = NULL;
	if (pipe_create(&pipe)) {
		pipeclose(pipe, &pipe->bufferA, &pipe->bufferB);
		pipeclose(pipe, &pipe->bufferB, &pipe->bufferA);
		return (ENFILE);
	}
	
	error = falloc(td->td_lwp, &rf, &fd1);
	if (error) {
		pipeclose(pipe, &pipe->bufferA, &pipe->bufferB);
		pipeclose(pipe, &pipe->bufferB, &pipe->bufferA);
		return (error);
	}
	fds[0] = fd1;

	/*
	 * Warning: once we've gotten past allocation of the fd for the
	 * read-side, we can only drop the read side via fdrop() in order
	 * to avoid races against processes which manage to dup() the read
	 * side while we are blocked trying to allocate the write side.
	 */
	rf->f_type = DTYPE_PIPE;
	rf->f_flag = FREAD | FWRITE;
	rf->f_ops = &pipeops;
	rf->f_data = (void *)((intptr_t)pipe | 0);
	if (flags & O_NONBLOCK)
		rf->f_flag |= O_NONBLOCK;
	if (flags & O_CLOEXEC)
		fdp->fd_files[fd1].fileflags |= UF_EXCLOSE;

	error = falloc(td->td_lwp, &wf, &fd2);
	if (error) {
		fsetfd(fdp, NULL, fd1);
		fdrop(rf);
		/* pipeA has been closed by fdrop() */
		/* close pipeB here */
		pipeclose(pipe, &pipe->bufferB, &pipe->bufferA);
		return (error);
	}
	wf->f_type = DTYPE_PIPE;
	wf->f_flag = FREAD | FWRITE;
	wf->f_ops = &pipeops;
	wf->f_data = (void *)((intptr_t)pipe | 1);
	if (flags & O_NONBLOCK)
		wf->f_flag |= O_NONBLOCK;
	if (flags & O_CLOEXEC)
		fdp->fd_files[fd2].fileflags |= UF_EXCLOSE;

	fds[1] = fd2;

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
 * [re]allocates KVA for the pipe's circular buffer.  The space is
 * pageable.  Called twice to setup full-duplex communications.
 *
 * NOTE: Independent vm_object's are used to improve performance.
 *
 * Returns 0 on success, ENOMEM on failure.
 */
static int
pipespace(struct pipe *pipe, struct pipebuf *pb, size_t size)
{
	struct vm_object *object;
	caddr_t buffer;
	vm_pindex_t npages;
	int error;

	size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
	if (size < 16384)
		size = 16384;
	if (size > 1024*1024)
		size = 1024*1024;

	npages = round_page(size) / PAGE_SIZE;
	object = pb->object;

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
				    PAGE_SIZE, TRUE,
				    VM_MAPTYPE_NORMAL, VM_SUBSYS_PIPE,
				    VM_PROT_ALL, VM_PROT_ALL, 0);

		if (error != KERN_SUCCESS) {
			vm_object_deallocate(object);
			return (ENOMEM);
		}
		pipe_free_kmem(pb);
		pb->object = object;
		pb->buffer = buffer;
		pb->size = size;
	}
	pb->rindex = 0;
	pb->windex = 0;

	return (0);
}

/*
 * Initialize and allocate VM and memory for pipe, pulling the pipe from
 * our per-cpu cache if possible.
 *
 * Returns 0 on success, else an error code (typically ENOMEM).  Caller
 * must still deallocate the pipe on failure.
 */
static int
pipe_create(struct pipe **pipep)
{
	globaldata_t gd = mycpu;
	struct pipe *pipe;
	int error;

	if ((pipe = gd->gd_pipeq) != NULL) {
		gd->gd_pipeq = pipe->next;
		--gd->gd_pipeqcount;
		pipe->next = NULL;
	} else {
		pipe = kmalloc(sizeof(*pipe), M_PIPE, M_WAITOK | M_ZERO);
		pipe->inum = gd->gd_anoninum++ * ncpus + gd->gd_cpuid + 2;
		lwkt_token_init(&pipe->bufferA.rlock, "piper");
		lwkt_token_init(&pipe->bufferA.wlock, "pipew");
		lwkt_token_init(&pipe->bufferB.rlock, "piper");
		lwkt_token_init(&pipe->bufferB.wlock, "pipew");
	}
	*pipep = pipe;
	if ((error = pipespace(pipe, &pipe->bufferA, pipe_size)) != 0) {
		return (error);
	}
	if ((error = pipespace(pipe, &pipe->bufferB, pipe_size)) != 0) {
		return (error);
	}
	vfs_timestamp(&pipe->ctime);
	pipe->bufferA.atime = pipe->ctime;
	pipe->bufferA.mtime = pipe->ctime;
	pipe->bufferB.atime = pipe->ctime;
	pipe->bufferB.mtime = pipe->ctime;
	pipe->open_count = 2;

	return (0);
}

/*
 * Read data from a pipe
 */
static int
pipe_read(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;
	size_t nread = 0;
	size_t size;	/* total bytes available */
	size_t nsize;	/* total bytes to read */
	size_t rindex;	/* contiguous bytes available */
	int notify_writer;
	int bigread;
	int bigcount;
	int error;
	int nbio;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}
	atomic_set_int(&curthread->td_mpflags, TDF_MP_BATCH_DEMARC);

	if (uio->uio_resid == 0)
		return(0);

	/*
	 * Calculate nbio
	 */
	if (fflags & O_FBLOCKING)
		nbio = 0;
	else if (fflags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * 'quick' NBIO test before things get expensive.
	 */
	if (nbio && rpb->rindex == rpb->windex &&
	    (rpb->state & PIPE_REOF) == 0) {
		return EAGAIN;
	}

	/*
	 * Reads are serialized.  Note however that buffer.buffer and
	 * buffer.size can change out from under us when the number
	 * of bytes in the buffer are zero due to the write-side doing a
	 * pipespace().
	 */
	lwkt_gettoken(&rpb->rlock);
	error = pipe_start_uio(&rpb->rip);
	if (error) {
		lwkt_reltoken(&rpb->rlock);
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

		/*
		 * lfence required to avoid read-reordering of buffer
		 * contents prior to validation of size.
		 */
		size = rpb->windex - rpb->rindex;
		cpu_lfence();
		if (size) {
			rindex = rpb->rindex & (rpb->size - 1);
			nsize = size;
			if (nsize > rpb->size - rindex)
				nsize = rpb->size - rindex;
			nsize = szmin(nsize, uio->uio_resid);

			/*
			 * Limit how much we move in one go so we have a
			 * chance to kick the writer while data is still
			 * available in the pipe.  This avoids getting into
			 * a ping-pong with the writer.
			 */
			if (nsize > (rpb->size >> 1))
				nsize = rpb->size >> 1;

			error = uiomove(&rpb->buffer[rindex], nsize, uio);
			if (error)
				break;
			rpb->rindex += nsize;
			nread += nsize;

			/*
			 * If the FIFO is still over half full just continue
			 * and do not try to notify the writer yet.  If
			 * less than half full notify any waiting writer.
			 */
			if (size - nsize > (rpb->size >> 1)) {
				notify_writer = 0;
			} else {
				notify_writer = 1;
				pipesignal(rpb, PIPE_WANTW);
			}
			continue;
		}

		/*
		 * If the "write-side" was blocked we wake it up.  This code
		 * is reached when the buffer is completely emptied.
		 */
		pipesignal(rpb, PIPE_WANTW);

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
		if (rpb->windex != rpb->rindex)
			continue;

#ifdef _RDTSC_SUPPORTED_
		if (pipe_delay) {
			int64_t tsc_target;
			int good = 0;

			tsc_target = tsc_get_target(pipe_delay);
			while (tsc_test_target(tsc_target) == 0) {
				cpu_lfence();
				if (rpb->windex != rpb->rindex) {
					good = 1;
					break;
				}
				cpu_pause();
			}
			if (good)
				continue;
		}
#endif

		/*
		 * Detect EOF condition, do not set error.
		 */
		if (rpb->state & PIPE_REOF)
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
		 * Last chance, interlock with WANTR
		 */
		tsleep_interlock(rpb, PCATCH);
		atomic_set_int(&rpb->state, PIPE_WANTR);

		/*
		 * Retest bytes available after memory barrier above.
		 */
		size = rpb->windex - rpb->rindex;
		if (size)
			continue;

		/*
		 * Retest EOF after memory barrier above.
		 */
		if (rpb->state & PIPE_REOF)
			break;

		/*
		 * Wait for more data or state change
		 */
		error = tsleep(rpb, PCATCH | PINTERLOCKED, "piperd", 0);
		if (error)
			break;
	}
	pipe_end_uio(&rpb->rip);

	/*
	 * Uptime last access time
	 */
	if (error == 0 && nread && rpb->lticks != ticks) {
		vfs_timestamp(&rpb->atime);
		rpb->lticks = ticks;
	}

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
		pipesignal(rpb, PIPE_WANTW);

		/*
		 * But we may also have to deal with a kqueue which is
		 * stored on the same pipe as its descriptor, so a
		 * EVFILT_WRITE event waiting for our side to drain will
		 * be on the other side.
		 */
		pipewakeup(wpb, 0);
	}
	/*size = rpb->windex - rpb->rindex;*/
	lwkt_reltoken(&rpb->rlock);

	return (error);
}

static int
pipe_write(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;
	size_t windex;
	size_t space;
	size_t wcount;
	size_t orig_resid;
	int bigwrite;
	int bigcount;
	int error;
	int nbio;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	/*
	 * Calculate nbio
	 */
	if (fflags & O_FBLOCKING)
		nbio = 0;
	else if (fflags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	/*
	 * 'quick' NBIO test before things get expensive.
	 */
	if (nbio && wpb->size == (wpb->windex - wpb->rindex) &&
	    uio->uio_resid && (wpb->state & PIPE_WEOF) == 0) {
		return EAGAIN;
	}

	/*
	 * Writes go to the peer.  The peer will always exist.
	 */
	lwkt_gettoken(&wpb->wlock);
	if (wpb->state & PIPE_WEOF) {
		lwkt_reltoken(&wpb->wlock);
		return (EPIPE);
	}

	/*
	 * Degenerate case (EPIPE takes prec)
	 */
	if (uio->uio_resid == 0) {
		lwkt_reltoken(&wpb->wlock);
		return(0);
	}

	/*
	 * Writes are serialized (start_uio must be called with wlock)
	 */
	error = pipe_start_uio(&wpb->wip);
	if (error) {
		lwkt_reltoken(&wpb->wlock);
		return (error);
	}

	orig_resid = uio->uio_resid;
	wcount = 0;

	bigwrite = (uio->uio_resid > 10 * 1024 * 1024);
	bigcount = 10;

	while (uio->uio_resid) {
		if (wpb->state & PIPE_WEOF) {
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

		windex = wpb->windex & (wpb->size - 1);
		space = wpb->size - (wpb->windex - wpb->rindex);

		/*
		 * Writes of size <= PIPE_BUF must be atomic.
		 */
		if ((space < uio->uio_resid) && (orig_resid <= PIPE_BUF))
			space = 0;

		/* 
		 * Write to fill, read size handles write hysteresis.  Also
		 * additional restrictions can cause select-based non-blocking
		 * writes to spin.
		 */
		if (space > 0) {
			size_t segsize;

			/*
			 * We want to notify a potentially waiting reader
			 * before we exhaust the write buffer for SMP
			 * pipelining.  Otherwise the write/read will begin
			 * to ping-pong.
			 */
			space = szmin(space, uio->uio_resid);
			if (space > (wpb->size >> 1))
				space = (wpb->size >> 1);

			/*
			 * First segment to transfer is minimum of
			 * transfer size and contiguous space in
			 * pipe buffer.  If first segment to transfer
			 * is less than the transfer size, we've got
			 * a wraparound in the buffer.
			 */
			segsize = wpb->size - windex;
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
			 */
			if (wcount == 0)
				pipesignal(wpb, PIPE_WANTR);

			/*
			 * Transfer segment, which may include a wrap-around.
			 * Update windex to account for both all in one go
			 * so the reader can read() the data atomically.
			 */
			error = uiomove(&wpb->buffer[windex], segsize, uio);
			if (error == 0 && segsize < space) {
				segsize = space - segsize;
				error = uiomove(&wpb->buffer[0], segsize, uio);
			}
			if (error)
				break;

			/*
			 * Memory fence prior to windex updating (note: not
			 * needed so this is a NOP on Intel).
			 */
			cpu_sfence();
			wpb->windex += space;

			/*
			 * Signal reader
			 */
			if (wcount != 0)
				pipesignal(wpb, PIPE_WANTR);
			wcount += space;
			continue;
		}

		/*
		 * Wakeup any pending reader
		 */
		pipesignal(wpb, PIPE_WANTR);

		/*
		 * don't block on non-blocking I/O
		 */
		if (nbio) {
			error = EAGAIN;
			break;
		}

#ifdef _RDTSC_SUPPORTED_
		if (pipe_delay) {
			int64_t tsc_target;
			int good = 0;

			tsc_target = tsc_get_target(pipe_delay);
			while (tsc_test_target(tsc_target) == 0) {
				cpu_lfence();
				space = wpb->size - (wpb->windex - wpb->rindex);
				if ((space < uio->uio_resid) &&
				    (orig_resid <= PIPE_BUF)) {
					space = 0;
				}
				if (space) {
					good = 1;
					break;
				}
				cpu_pause();
			}
			if (good)
				continue;
		}
#endif

		/*
		 * Interlocked test.   Atomic op enforces the memory barrier.
		 */
		tsleep_interlock(wpb, PCATCH);
		atomic_set_int(&wpb->state, PIPE_WANTW);

		/*
		 * Retest space available after memory barrier above.
		 * Writes of size <= PIPE_BUF must be atomic.
		 */
		space = wpb->size - (wpb->windex - wpb->rindex);
		if ((space < uio->uio_resid) && (orig_resid <= PIPE_BUF))
			space = 0;

		/*
		 * Retest EOF after memory barrier above.
		 */
		if (wpb->state & PIPE_WEOF) {
			error = EPIPE;
			break;
		}

		/*
		 * We have no more space and have something to offer,
		 * wake up select/poll/kq.
		 */
		if (space == 0) {
			pipewakeup(wpb, 1);
			error = tsleep(wpb, PCATCH | PINTERLOCKED, "pipewr", 0);
		}

		/*
		 * Break out if we errored or the read side wants us to go
		 * away.
		 */
		if (error)
			break;
		if (wpb->state & PIPE_WEOF) {
			error = EPIPE;
			break;
		}
	}
	pipe_end_uio(&wpb->wip);

	/*
	 * If we have put any characters in the buffer, we wake up
	 * the reader.
	 *
	 * Both rlock and wlock are required to be able to modify pipe_state.
	 */
	if (wpb->windex != wpb->rindex) {
		pipesignal(wpb, PIPE_WANTR);
		pipewakeup(wpb, 1);
	}

	/*
	 * Don't return EPIPE if I/O was successful
	 */
	if ((wpb->rindex == wpb->windex) &&
	    (uio->uio_resid == 0) &&
	    (error == EPIPE)) {
		error = 0;
	}

	if (error == 0 && wpb->lticks != ticks) {
		vfs_timestamp(&wpb->mtime);
		wpb->lticks = ticks;
	}

	/*
	 * We have something to offer,
	 * wake up select/poll/kq.
	 */
	/*space = wpb->windex - wpb->rindex;*/
	lwkt_reltoken(&wpb->wlock);

	return (error);
}

/*
 * we implement a very minimal set of ioctls for compatibility with sockets.
 */
static int
pipe_ioctl(struct file *fp, u_long cmd, caddr_t data,
	   struct ucred *cred, struct sysmsg *msg)
{
	struct pipebuf *rpb;
	struct pipe *pipe;
	int error;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
	} else {
		rpb = &pipe->bufferA;
	}

	lwkt_gettoken(&rpb->rlock);
	lwkt_gettoken(&rpb->wlock);

	switch (cmd) {
	case FIOASYNC:
		if (*(int *)data) {
			atomic_set_int(&rpb->state, PIPE_ASYNC);
		} else {
			atomic_clear_int(&rpb->state, PIPE_ASYNC);
		}
		error = 0;
		break;
	case FIONREAD:
		*(int *)data = (int)(rpb->windex - rpb->rindex);
		error = 0;
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)data, &rpb->sigio);
		break;
	case FIOGETOWN:
		*(int *)data = fgetown(&rpb->sigio);
		error = 0;
		break;
	case TIOCSPGRP:
		/* This is deprecated, FIOSETOWN should be used instead. */
		error = fsetown(-(*(int *)data), &rpb->sigio);
		break;

	case TIOCGPGRP:
		/* This is deprecated, FIOGETOWN should be used instead. */
		*(int *)data = -fgetown(&rpb->sigio);
		error = 0;
		break;
	default:
		error = ENOTTY;
		break;
	}
	lwkt_reltoken(&rpb->wlock);
	lwkt_reltoken(&rpb->rlock);

	return (error);
}

/*
 * MPSAFE
 */
static int
pipe_stat(struct file *fp, struct stat *ub, struct ucred *cred)
{
	struct pipebuf *rpb;
	struct pipe *pipe;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
	} else {
		rpb = &pipe->bufferA;
	}

	bzero((caddr_t)ub, sizeof(*ub));
	ub->st_mode = S_IFIFO;
	ub->st_blksize = rpb->size;
	ub->st_size = rpb->windex - rpb->rindex;
	ub->st_blocks = (ub->st_size + ub->st_blksize - 1) / ub->st_blksize;
	ub->st_atimespec = rpb->atime;
	ub->st_mtimespec = rpb->mtime;
	ub->st_ctimespec = pipe->ctime;
	ub->st_uid = fp->f_cred->cr_uid;
	ub->st_gid = fp->f_cred->cr_gid;
	ub->st_ino = pipe->inum;
	/*
	 * Left as 0: st_dev, st_nlink, st_rdev,
	 * st_flags, st_gen.
	 * XXX (st_dev, st_ino) should be unique.
	 */

	return (0);
}

static int
pipe_close(struct file *fp)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	funsetown(&rpb->sigio);
	pipeclose(pipe, rpb, wpb);

	return (0);
}

/*
 * Shutdown one or both directions of a full-duplex pipe.
 */
static int
pipe_shutdown(struct file *fp, int how)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;
	int error = EPIPE;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	/*
	 * We modify pipe_state on both pipes, which means we need
	 * all four tokens!
	 */
	lwkt_gettoken(&rpb->rlock);
	lwkt_gettoken(&rpb->wlock);
	lwkt_gettoken(&wpb->rlock);
	lwkt_gettoken(&wpb->wlock);

	switch(how) {
	case SHUT_RDWR:
	case SHUT_RD:
		/*
		 * EOF on my reads and peer writes
		 */
		atomic_set_int(&rpb->state, PIPE_REOF | PIPE_WEOF);
		if (rpb->state & PIPE_WANTR) {
			rpb->state &= ~PIPE_WANTR;
			wakeup(rpb);
		}
		if (rpb->state & PIPE_WANTW) {
			rpb->state &= ~PIPE_WANTW;
			wakeup(rpb);
		}
		error = 0;
		if (how == SHUT_RD)
			break;
		/* fall through */
	case SHUT_WR:
		/*
		 * EOF on peer reads and my writes
		 */
		atomic_set_int(&wpb->state, PIPE_REOF | PIPE_WEOF);
		if (wpb->state & PIPE_WANTR) {
			wpb->state &= ~PIPE_WANTR;
			wakeup(wpb);
		}
		if (wpb->state & PIPE_WANTW) {
			wpb->state &= ~PIPE_WANTW;
			wakeup(wpb);
		}
		error = 0;
		break;
	}
	pipewakeup(rpb, 1);
	pipewakeup(wpb, 1);

	lwkt_reltoken(&wpb->wlock);
	lwkt_reltoken(&wpb->rlock);
	lwkt_reltoken(&rpb->wlock);
	lwkt_reltoken(&rpb->rlock);

	return (error);
}

/*
 * Destroy the pipe buffer.
 */
static void
pipe_free_kmem(struct pipebuf *pb)
{
	if (pb->buffer != NULL) {
		kmem_free(&kernel_map, (vm_offset_t)pb->buffer, pb->size);
		pb->buffer = NULL;
		pb->object = NULL;
	}
}

/*
 * Close one half of the pipe.  We are closing the pipe for reading on rpb
 * and writing on wpb.  This routine must be called twice with the pipebufs
 * reversed to close both directions.
 */
static void
pipeclose(struct pipe *pipe, struct pipebuf *rpb, struct pipebuf *wpb)
{
	globaldata_t gd;

	if (pipe == NULL)
		return;

	/*
	 * We need both the read and write tokens to modify pipe_state.
	 */
	lwkt_gettoken(&rpb->rlock);
	lwkt_gettoken(&rpb->wlock);

	/*
	 * Set our state, wakeup anyone waiting in select/poll/kq, and
	 * wakeup anyone blocked on our pipe.  No action if our side
	 * is already closed.
	 */
	if (rpb->state & PIPE_CLOSED) {
		lwkt_reltoken(&rpb->wlock);
		lwkt_reltoken(&rpb->rlock);
		return;
	}

	atomic_set_int(&rpb->state, PIPE_CLOSED | PIPE_REOF | PIPE_WEOF);
	pipewakeup(rpb, 1);
	if (rpb->state & (PIPE_WANTR | PIPE_WANTW)) {
		rpb->state &= ~(PIPE_WANTR | PIPE_WANTW);
		wakeup(rpb);
	}
	lwkt_reltoken(&rpb->wlock);
	lwkt_reltoken(&rpb->rlock);

	/*
	 * Disconnect from peer.
	 */
	lwkt_gettoken(&wpb->rlock);
	lwkt_gettoken(&wpb->wlock);

	atomic_set_int(&wpb->state, PIPE_REOF | PIPE_WEOF);
	pipewakeup(wpb, 1);
	if (wpb->state & (PIPE_WANTR | PIPE_WANTW)) {
		wpb->state &= ~(PIPE_WANTR | PIPE_WANTW);
		wakeup(wpb);
	}
	if (SLIST_FIRST(&wpb->kq.ki_note))
		KNOTE(&wpb->kq.ki_note, 0);
	lwkt_reltoken(&wpb->wlock);
	lwkt_reltoken(&wpb->rlock);

	/*
	 * Free resources once both sides are closed.  We maintain a pcpu
	 * cache to improve performance, so the actual tear-down case is
	 * limited to bulk situations.
	 *
	 * However, the bulk tear-down case can cause intense contention
	 * on the kernel_map when, e.g. hundreds to hundreds of thousands
	 * of processes are killed at the same time.  To deal with this we
	 * use a pcpu mutex to maintain concurrency but also limit the
	 * number of threads banging on the map and pmap.
	 *
	 * We use the mtx mechanism instead of the lockmgr mechanism because
	 * the mtx mechanism utilizes a queued design which will not break
	 * down in the face of thousands to hundreds of thousands of
	 * processes trying to free pipes simultaneously.  The lockmgr
	 * mechanism will wind up waking them all up each time a lock
	 * cycles.
	 */
	if (atomic_fetchadd_int(&pipe->open_count, -1) == 1) {
		gd = mycpu;
		if (gd->gd_pipeqcount >= pipe_maxcache) {
			mtx_lock(&pipe_gdlocks[gd->gd_cpuid].mtx);
			pipe_free_kmem(rpb);
			pipe_free_kmem(wpb);
			mtx_unlock(&pipe_gdlocks[gd->gd_cpuid].mtx);
			kfree(pipe, M_PIPE);
		} else {
			rpb->state = 0;
			wpb->state = 0;
			pipe->next = gd->gd_pipeq;
			gd->gd_pipeq = pipe;
			++gd->gd_pipeqcount;
		}
	}
}

static int
pipe_kqfilter(struct file *fp, struct knote *kn)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;

	pipe = (struct pipe *)((intptr_t)fp->f_data & ~(intptr_t)1);
	if ((intptr_t)fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &pipe_rfiltops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &pipe_wfiltops;
		if (wpb->state & PIPE_CLOSED) {
			/* other end of pipe has been closed */
			return (EPIPE);
		}
		break;
	default:
		return (EOPNOTSUPP);
	}

	if (rpb == &pipe->bufferA)
		kn->kn_hook = (caddr_t)(void *)((intptr_t)pipe | 0);
	else
		kn->kn_hook = (caddr_t)(void *)((intptr_t)pipe | 1);

	knote_insert(&rpb->kq.ki_note, kn);

	return (0);
}

static void
filt_pipedetach(struct knote *kn)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;

	pipe = (struct pipe *)((intptr_t)kn->kn_hook & ~(intptr_t)1);
	if ((intptr_t)kn->kn_hook & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}
	knote_remove(&rpb->kq.ki_note, kn);
}

/*ARGSUSED*/
static int
filt_piperead(struct knote *kn, long hint)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;
	int ready = 0;

	pipe = (struct pipe *)((intptr_t)kn->kn_fp->f_data & ~(intptr_t)1);
	if ((intptr_t)kn->kn_fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	/*
	 * We shouldn't need the pipe locks because the knote itself is
	 * locked via KN_PROCESSING.  If we lose a race against the writer,
	 * the writer will just issue a KNOTE() after us.
	 */
#if 0
	lwkt_gettoken(&rpb->rlock);
	lwkt_gettoken(&rpb->wlock);
#endif

	kn->kn_data = rpb->windex - rpb->rindex;
	if (kn->kn_data < 0)
		kn->kn_data = 0;

	if (rpb->state & PIPE_REOF) {
		/*
		 * Only set NODATA if all data has been exhausted
		 */
		if (kn->kn_data == 0)
			kn->kn_flags |= EV_NODATA;
		kn->kn_flags |= EV_EOF; 
		ready = 1;
	}

#if 0
	lwkt_reltoken(&rpb->wlock);
	lwkt_reltoken(&rpb->rlock);
#endif

	if (!ready)
		ready = kn->kn_data > 0;

	return (ready);
}

/*ARGSUSED*/
static int
filt_pipewrite(struct knote *kn, long hint)
{
	struct pipebuf *rpb;
	struct pipebuf *wpb;
	struct pipe *pipe;
	int ready = 0;

	pipe = (struct pipe *)((intptr_t)kn->kn_fp->f_data & ~(intptr_t)1);
	if ((intptr_t)kn->kn_fp->f_data & 1) {
		rpb = &pipe->bufferB;
		wpb = &pipe->bufferA;
	} else {
		rpb = &pipe->bufferA;
		wpb = &pipe->bufferB;
	}

	kn->kn_data = 0;
	if (wpb->state & PIPE_CLOSED) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}

	/*
	 * We shouldn't need the pipe locks because the knote itself is
	 * locked via KN_PROCESSING.  If we lose a race against the reader,
	 * the writer will just issue a KNOTE() after us.
	 */
#if 0
	lwkt_gettoken(&wpb->rlock);
	lwkt_gettoken(&wpb->wlock);
#endif

	if (wpb->state & PIPE_WEOF) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		ready = 1;
	}

	if (!ready) {
		kn->kn_data = wpb->size - (wpb->windex - wpb->rindex);
		if (kn->kn_data < 0)
			kn->kn_data = 0;
	}

#if 0
	lwkt_reltoken(&wpb->wlock);
	lwkt_reltoken(&wpb->rlock);
#endif

	if (!ready)
		ready = kn->kn_data >= PIPE_BUF;

	return (ready);
}
