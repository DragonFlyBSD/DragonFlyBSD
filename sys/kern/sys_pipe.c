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
 * $DragonFly: src/sys/kern/sys_pipe.c,v 1.50 2008/09/09 04:06:13 dillon Exp $
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
#include <sys/poll.h>
#include <sys/select.h>
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
static int pipe_poll (struct file *fp, int events, struct ucred *cred);
static int pipe_kqfilter (struct file *fp, struct knote *kn);
static int pipe_stat (struct file *fp, struct stat *sb, struct ucred *cred);
static int pipe_ioctl (struct file *fp, u_long cmd, caddr_t data, struct ucred *cred);

static struct fileops pipeops = {
	.fo_read = pipe_read, 
	.fo_write = pipe_write,
	.fo_ioctl = pipe_ioctl,
	.fo_poll = pipe_poll,
	.fo_kqfilter = pipe_kqfilter,
	.fo_stat = pipe_stat,
	.fo_close = pipe_close,
	.fo_shutdown = pipe_shutdown
};

static void	filt_pipedetach(struct knote *kn);
static int	filt_piperead(struct knote *kn, long hint);
static int	filt_pipewrite(struct knote *kn, long hint);

static struct filterops pipe_rfiltops =
	{ 1, NULL, filt_pipedetach, filt_piperead };
static struct filterops pipe_wfiltops =
	{ 1, NULL, filt_pipedetach, filt_pipewrite };

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

SYSCTL_NODE(_kern, OID_AUTO, pipe, CTLFLAG_RW, 0, "Pipe operation");
SYSCTL_INT(_kern_pipe, OID_AUTO, nbig,
        CTLFLAG_RD, &pipe_nbig, 0, "numer of big pipes allocated");
SYSCTL_INT(_kern_pipe, OID_AUTO, bigcount,
        CTLFLAG_RW, &pipe_bigcount, 0, "number of times pipe expanded");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxcache,
        CTLFLAG_RW, &pipe_maxcache, 0, "max pipes cached per-cpu");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxbig,
        CTLFLAG_RW, &pipe_maxbig, 0, "max number of big pipes");
#if !defined(NO_PIPE_SYSCTL_STATS)
SYSCTL_INT(_kern_pipe, OID_AUTO, bcache_alloc,
        CTLFLAG_RW, &pipe_bcache_alloc, 0, "pipe buffer from pcpu cache");
SYSCTL_INT(_kern_pipe, OID_AUTO, bkmem_alloc,
        CTLFLAG_RW, &pipe_bkmem_alloc, 0, "pipe buffer from kmem");
#endif

static void pipeclose (struct pipe *cpipe);
static void pipe_free_kmem (struct pipe *cpipe);
static int pipe_create (struct pipe **cpipep);
static __inline int pipelock (struct pipe *cpipe, int catch);
static __inline void pipeunlock (struct pipe *cpipe);
static __inline void pipeselwakeup (struct pipe *cpipe);
static int pipespace (struct pipe *cpipe, int size);

/*
 * The pipe system call for the DTYPE_PIPE type of pipes
 *
 * pipe_ARgs(int dummy)
 */

/* ARGSUSED */
int
sys_pipe(struct pipe_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *rf, *wf;
	struct pipe *rpipe, *wpipe;
	int fd1, fd2, error;

	KKASSERT(p);

	rpipe = wpipe = NULL;
	if (pipe_create(&rpipe) || pipe_create(&wpipe)) {
		pipeclose(rpipe); 
		pipeclose(wpipe); 
		return (ENFILE);
	}
	
	error = falloc(p, &rf, &fd1);
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
	error = falloc(p, &wf, &fd2);
	if (error) {
		fsetfd(p, NULL, fd1);
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

	rpipe->pipe_peer = wpipe;
	wpipe->pipe_peer = rpipe;

	fsetfd(p, rf, fd1);
	fsetfd(p, wf, fd2);
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

		error = vm_map_find(&kernel_map, object, 0,
				    (vm_offset_t *)&buffer, size,
				    1,
				    VM_MAPTYPE_NORMAL,
				    VM_PROT_ALL, VM_PROT_ALL,
				    0);

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
	} else {
		cpipe = kmalloc(sizeof(struct pipe), M_PIPE, M_WAITOK|M_ZERO);
	}
	*cpipep = cpipe;
	if ((error = pipespace(cpipe, PIPE_SIZE)) != 0)
		return (error);
	vfs_timestamp(&cpipe->pipe_ctime);
	cpipe->pipe_atime = cpipe->pipe_ctime;
	cpipe->pipe_mtime = cpipe->pipe_ctime;
	return (0);
}


/*
 * lock a pipe for I/O, blocking other access
 */
static __inline int
pipelock(struct pipe *cpipe, int catch)
{
	int error;

	while (cpipe->pipe_state & PIPE_LOCK) {
		cpipe->pipe_state |= PIPE_LWANT;
		error = tsleep(cpipe, (catch ? PCATCH : 0), "pipelk", 0);
		if (error != 0) 
			return (error);
	}
	cpipe->pipe_state |= PIPE_LOCK;
	return (0);
}

/*
 * unlock a pipe I/O lock
 */
static __inline void
pipeunlock(struct pipe *cpipe)
{

	cpipe->pipe_state &= ~PIPE_LOCK;
	if (cpipe->pipe_state & PIPE_LWANT) {
		cpipe->pipe_state &= ~PIPE_LWANT;
		wakeup(cpipe);
	}
}

static __inline void
pipeselwakeup(struct pipe *cpipe)
{

	if (cpipe->pipe_state & PIPE_SEL) {
		cpipe->pipe_state &= ~PIPE_SEL;
		selwakeup(&cpipe->pipe_sel);
	}
	if ((cpipe->pipe_state & PIPE_ASYNC) && cpipe->pipe_sigio)
		pgsigio(cpipe->pipe_sigio, SIGIO, 0);
	KNOTE(&cpipe->pipe_sel.si_note, 0);
}

/*
 * MPALMOSTSAFE (acquires mplock)
 */
static int
pipe_read(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct pipe *rpipe;
	int error;
	int nread = 0;
	int nbio;
	u_int size;	/* total bytes available */
	u_int rindex;	/* contiguous bytes available */

	get_mplock();
	rpipe = (struct pipe *) fp->f_data;
	++rpipe->pipe_busy;
	error = pipelock(rpipe, 1);
	if (error)
		goto unlocked_error;

	if (fflags & O_FBLOCKING)
		nbio = 0;
	else if (fflags & O_FNONBLOCKING)
		nbio = 1;
	else if (fp->f_flag & O_NONBLOCK)
		nbio = 1;
	else
		nbio = 0;

	while (uio->uio_resid) {
		size = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;
		if (size) {
			rindex = rpipe->pipe_buffer.rindex &
				 (rpipe->pipe_buffer.size - 1);
			if (size > rpipe->pipe_buffer.size - rindex)
				size = rpipe->pipe_buffer.size - rindex;
			if (size > (u_int)uio->uio_resid)
				size = (u_int)uio->uio_resid;

			error = uiomove(&rpipe->pipe_buffer.buffer[rindex],
					size, uio);
			if (error)
				break;
			rpipe->pipe_buffer.rindex += size;

			/*
			 * If there is no more to read in the pipe, reset
			 * its pointers to the beginning.  This improves
			 * cache hit stats.
			 */
			if (rpipe->pipe_buffer.rindex ==
			    rpipe->pipe_buffer.windex) {
				rpipe->pipe_buffer.rindex = 0;
				rpipe->pipe_buffer.windex = 0;
			}
			nread += size;
		} else {
			/*
			 * detect EOF condition
			 * read returns 0 on EOF, no need to set error
			 */
			if (rpipe->pipe_state & PIPE_EOF)
				break;

			/*
			 * If the "write-side" has been blocked, wake it up now.
			 */
			if (rpipe->pipe_state & PIPE_WANTW) {
				rpipe->pipe_state &= ~PIPE_WANTW;
				wakeup(rpipe);
			}

			/*
			 * Break if some data was read.
			 */
			if (nread > 0)
				break;

			/*
			 * Unlock the pipe buffer for our remaining
			 * processing.  We will either break out with an
			 * error or we will sleep and relock to loop.
			 */
			pipeunlock(rpipe);

			/*
			 * Handle non-blocking mode operation or
			 * wait for more data.
			 */
			if (nbio) {
				error = EAGAIN;
			} else {
				rpipe->pipe_state |= PIPE_WANTR;
				if ((error = tsleep(rpipe, PCATCH,
						    "piperd", 0)) == 0) {
					error = pipelock(rpipe, 1);
				}
			}
			if (error)
				goto unlocked_error;
		}
	}
	pipeunlock(rpipe);

	if (error == 0)
		vfs_timestamp(&rpipe->pipe_atime);
unlocked_error:
	--rpipe->pipe_busy;

	/*
	 * PIPE_WANT processing only makes sense if pipe_busy is 0.
	 */
	size = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;

	if ((rpipe->pipe_busy == 0) && (rpipe->pipe_state & PIPE_WANT)) {
		rpipe->pipe_state &= ~(PIPE_WANT|PIPE_WANTW);
		wakeup(rpipe);
	} else if (size < MINPIPESIZE) {
		/*
		 * Handle write blocking hysteresis.
		 */
		if (rpipe->pipe_state & PIPE_WANTW) {
			rpipe->pipe_state &= ~PIPE_WANTW;
			wakeup(rpipe);
		}
	}

	if ((rpipe->pipe_buffer.size - size) >= PIPE_BUF)
		pipeselwakeup(rpipe);
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
pipe_write(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	int error = 0;
	int orig_resid;
	int nbio;
	struct pipe *wpipe, *rpipe;
	u_int windex;
	u_int space;

	get_mplock();
	rpipe = (struct pipe *) fp->f_data;
	wpipe = rpipe->pipe_peer;

	/*
	 * detect loss of pipe read side, issue SIGPIPE if lost.
	 */
	if ((wpipe == NULL) || (wpipe->pipe_state & PIPE_EOF)) {
		rel_mplock();
		return (EPIPE);
	}
	++wpipe->pipe_busy;

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
	 * so.
	 */
	if ((uio->uio_resid > PIPE_SIZE) &&
	    (pipe_nbig < pipe_maxbig) &&
	    (wpipe->pipe_buffer.size <= PIPE_SIZE) &&
	    (wpipe->pipe_buffer.rindex == wpipe->pipe_buffer.windex) &&
	    (error = pipelock(wpipe, 1)) == 0) {
		/* 
		 * Recheck after lock.
		 */
		if ((pipe_nbig < pipe_maxbig) &&
		    (wpipe->pipe_buffer.size <= PIPE_SIZE) &&
		    (wpipe->pipe_buffer.rindex == wpipe->pipe_buffer.windex)) {
			if (pipespace(wpipe, BIG_PIPE_SIZE) == 0) {
				++pipe_bigcount;
				pipe_nbig++;
			}
		}
		pipeunlock(wpipe);
	}

	/*
	 * If an early error occured unbusy and return, waking up any pending
	 * readers.
	 */
	if (error) {
		--wpipe->pipe_busy;
		if ((wpipe->pipe_busy == 0) && 
		    (wpipe->pipe_state & PIPE_WANT)) {
			wpipe->pipe_state &= ~(PIPE_WANT | PIPE_WANTR);
			wakeup(wpipe);
		}
		rel_mplock();
		return(error);
	}
		
	KASSERT(wpipe->pipe_buffer.buffer != NULL, ("pipe buffer gone"));

	orig_resid = uio->uio_resid;

	while (uio->uio_resid) {
		if (wpipe->pipe_state & PIPE_EOF) {
			error = EPIPE;
			break;
		}

		windex = wpipe->pipe_buffer.windex &
			 (wpipe->pipe_buffer.size - 1);
		space = wpipe->pipe_buffer.size -
			(wpipe->pipe_buffer.windex - wpipe->pipe_buffer.rindex);

		/* Writes of size <= PIPE_BUF must be atomic. */
		if ((space < uio->uio_resid) && (orig_resid <= PIPE_BUF))
			space = 0;

		/* 
		 * Write to fill, read size handles write hysteresis.  Also
		 * additional restrictions can cause select-based non-blocking
		 * writes to spin.
		 */
		if (space > 0) {
			if ((error = pipelock(wpipe,1)) == 0) {
				u_int segsize;

				/* 
				 * If a process blocked in uiomove, our
				 * value for space might be bad.
				 *
				 * XXX will we be ok if the reader has gone
				 * away here?
				 */
				if (space > (wpipe->pipe_buffer.size -
					     (wpipe->pipe_buffer.windex -
					      wpipe->pipe_buffer.rindex))) {
					pipeunlock(wpipe);
					continue;
				}
				windex = wpipe->pipe_buffer.windex &
					 (wpipe->pipe_buffer.size - 1);

				/*
				 * Transfer size is minimum of uio transfer
				 * and free space in pipe buffer.
				 */
				if (space > (u_int)uio->uio_resid)
					space = (u_int)uio->uio_resid;

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
				
				/* Transfer first segment */

				error = uiomove(
					    &wpipe->pipe_buffer.buffer[windex],
					    segsize, uio);
				
				if (error == 0 && segsize < space) {
					/* 
					 * Transfer remaining part now, to
					 * support atomic writes.  Wraparound
					 * happened.
					 */
					error = uiomove(&wpipe->pipe_buffer.
							  buffer[0],
							space - segsize, uio);
				}
				if (error == 0)
					wpipe->pipe_buffer.windex += space;
				pipeunlock(wpipe);
			}
			if (error)
				break;

		} else {
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
				error = EAGAIN;
				break;
			}

			/*
			 * We have no more space and have something to offer,
			 * wake up select/poll.
			 */
			pipeselwakeup(wpipe);

			wpipe->pipe_state |= PIPE_WANTW;
			error = tsleep(wpipe, PCATCH, "pipewr", 0);
			if (error != 0)
				break;
			/*
			 * If read side wants to go away, we just issue a signal
			 * to ourselves.
			 */
			if (wpipe->pipe_state & PIPE_EOF) {
				error = EPIPE;
				break;
			}	
		}
	}

	--wpipe->pipe_busy;

	if ((wpipe->pipe_busy == 0) && (wpipe->pipe_state & PIPE_WANT)) {
		wpipe->pipe_state &= ~(PIPE_WANT | PIPE_WANTR);
		wakeup(wpipe);
	} else if (wpipe->pipe_buffer.windex != wpipe->pipe_buffer.rindex) {
		/*
		 * If we have put any characters in the buffer, we wake up
		 * the reader.
		 */
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}
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
	 * wake up select/poll.
	 */
	if (wpipe->pipe_buffer.rindex != wpipe->pipe_buffer.windex)
		pipeselwakeup(wpipe);
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 *
 * we implement a very minimal set of ioctls for compatibility with sockets.
 */
int
pipe_ioctl(struct file *fp, u_long cmd, caddr_t data, struct ucred *cred)
{
	struct pipe *mpipe;
	int error;

	get_mplock();
	mpipe = (struct pipe *)fp->f_data;

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
		*(int *)data = fgetown(mpipe->pipe_sigio);
		error = 0;
		break;
	case TIOCSPGRP:
		/* This is deprecated, FIOSETOWN should be used instead. */
		error = fsetown(-(*(int *)data), &mpipe->pipe_sigio);
		break;

	case TIOCGPGRP:
		/* This is deprecated, FIOGETOWN should be used instead. */
		*(int *)data = -fgetown(mpipe->pipe_sigio);
		error = 0;
		break;
	default:
		error = ENOTTY;
		break;
	}
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
int
pipe_poll(struct file *fp, int events, struct ucred *cred)
{
	struct pipe *rpipe;
	struct pipe *wpipe;
	int revents = 0;
	u_int space;

	get_mplock();
	rpipe = (struct pipe *)fp->f_data;
	wpipe = rpipe->pipe_peer;
	if (events & (POLLIN | POLLRDNORM)) {
		if ((rpipe->pipe_buffer.windex != rpipe->pipe_buffer.rindex) ||
		    (rpipe->pipe_state & PIPE_EOF)) {
			revents |= events & (POLLIN | POLLRDNORM);
		}
	}

	if (events & (POLLOUT | POLLWRNORM)) {
		if (wpipe == NULL || (wpipe->pipe_state & PIPE_EOF)) {
			revents |= events & (POLLOUT | POLLWRNORM);
		} else {
			space = wpipe->pipe_buffer.windex -
				wpipe->pipe_buffer.rindex;
			space = wpipe->pipe_buffer.size - space;
			if (space >= PIPE_BUF)
				revents |= events & (POLLOUT | POLLWRNORM);
		}
	}

	if ((rpipe->pipe_state & PIPE_EOF) ||
	    (wpipe == NULL) ||
	    (wpipe->pipe_state & PIPE_EOF))
		revents |= POLLHUP;

	if (revents == 0) {
		if (events & (POLLIN | POLLRDNORM)) {
			selrecord(curthread, &rpipe->pipe_sel);
			rpipe->pipe_state |= PIPE_SEL;
		}

		if (events & (POLLOUT | POLLWRNORM)) {
			selrecord(curthread, &wpipe->pipe_sel);
			wpipe->pipe_state |= PIPE_SEL;
		}
	}
	rel_mplock();
	return (revents);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
pipe_stat(struct file *fp, struct stat *ub, struct ucred *cred)
{
	struct pipe *pipe;

	get_mplock();
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
	rel_mplock();
	return (0);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
pipe_close(struct file *fp)
{
	struct pipe *cpipe;

	get_mplock();
	cpipe = (struct pipe *)fp->f_data;
	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	funsetown(cpipe->pipe_sigio);
	pipeclose(cpipe);
	rel_mplock();
	return (0);
}

/*
 * Shutdown one or both directions of a full-duplex pipe.
 *
 * MPALMOSTSAFE - acquires mplock
 */
static int
pipe_shutdown(struct file *fp, int how)
{
	struct pipe *rpipe;
	struct pipe *wpipe;
	int error = EPIPE;

	get_mplock();
	rpipe = (struct pipe *)fp->f_data;

	switch(how) {
	case SHUT_RDWR:
	case SHUT_RD:
		if (rpipe) {
			rpipe->pipe_state |= PIPE_EOF;
			pipeselwakeup(rpipe);
			if (rpipe->pipe_busy)
				wakeup(rpipe);
			error = 0;
		}
		if (how == SHUT_RD)
			break;
		/* fall through */
	case SHUT_WR:
		if (rpipe && (wpipe = rpipe->pipe_peer) != NULL) {
			wpipe->pipe_state |= PIPE_EOF;
			pipeselwakeup(wpipe);
			if (wpipe->pipe_busy)
				wakeup(wpipe);
			error = 0;
		}
	}
	rel_mplock();
	return (error);
}

static void
pipe_free_kmem(struct pipe *cpipe)
{
	if (cpipe->pipe_buffer.buffer != NULL) {
		if (cpipe->pipe_buffer.size > PIPE_SIZE)
			--pipe_nbig;
		kmem_free(&kernel_map,
			(vm_offset_t)cpipe->pipe_buffer.buffer,
			cpipe->pipe_buffer.size);
		cpipe->pipe_buffer.buffer = NULL;
		cpipe->pipe_buffer.object = NULL;
	}
}

/*
 * shutdown the pipe
 */
static void
pipeclose(struct pipe *cpipe)
{
	globaldata_t gd;
	struct pipe *ppipe;

	if (cpipe == NULL)
		return;

	pipeselwakeup(cpipe);

	/*
	 * If the other side is blocked, wake it up saying that
	 * we want to close it down.
	 */
	while (cpipe->pipe_busy) {
		wakeup(cpipe);
		cpipe->pipe_state |= PIPE_WANT | PIPE_EOF;
		tsleep(cpipe, 0, "pipecl", 0);
	}

	/*
	 * Disconnect from peer
	 */
	if ((ppipe = cpipe->pipe_peer) != NULL) {
		pipeselwakeup(ppipe);

		ppipe->pipe_state |= PIPE_EOF;
		wakeup(ppipe);
		KNOTE(&ppipe->pipe_sel.si_note, 0);
		ppipe->pipe_peer = NULL;
	}

	if (cpipe->pipe_kva) {
		pmap_qremove(cpipe->pipe_kva, XIO_INTERNAL_PAGES);
		kmem_free(&kernel_map, cpipe->pipe_kva, XIO_INTERNAL_SIZE);
		cpipe->pipe_kva = 0;
	}

	/*
	 * free or cache resources
	 */
	gd = mycpu;
	if (gd->gd_pipeqcount >= pipe_maxcache ||
	    cpipe->pipe_buffer.size != PIPE_SIZE
	) {
		pipe_free_kmem(cpipe);
		kfree(cpipe, M_PIPE);
	} else {
		cpipe->pipe_state = 0;
		cpipe->pipe_busy = 0;
		cpipe->pipe_peer = gd->gd_pipeq;
		gd->gd_pipeq = cpipe;
		++gd->gd_pipeqcount;
	}
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
pipe_kqfilter(struct file *fp, struct knote *kn)
{
	struct pipe *cpipe;

	get_mplock();
	cpipe = (struct pipe *)kn->kn_fp->f_data;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &pipe_rfiltops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &pipe_wfiltops;
		cpipe = cpipe->pipe_peer;
		if (cpipe == NULL) {
			/* other end of pipe has been closed */
			rel_mplock();
			return (EPIPE);
		}
		break;
	default:
		return (1);
	}
	kn->kn_hook = (caddr_t)cpipe;

	SLIST_INSERT_HEAD(&cpipe->pipe_sel.si_note, kn, kn_selnext);
	rel_mplock();
	return (0);
}

static void
filt_pipedetach(struct knote *kn)
{
	struct pipe *cpipe = (struct pipe *)kn->kn_hook;

	SLIST_REMOVE(&cpipe->pipe_sel.si_note, kn, knote, kn_selnext);
}

/*ARGSUSED*/
static int
filt_piperead(struct knote *kn, long hint)
{
	struct pipe *rpipe = (struct pipe *)kn->kn_fp->f_data;
	struct pipe *wpipe = rpipe->pipe_peer;

	kn->kn_data = rpipe->pipe_buffer.windex - rpipe->pipe_buffer.rindex;

	if ((rpipe->pipe_state & PIPE_EOF) ||
	    (wpipe == NULL) || (wpipe->pipe_state & PIPE_EOF)) {
		kn->kn_flags |= EV_EOF; 
		return (1);
	}
	return (kn->kn_data > 0);
}

/*ARGSUSED*/
static int
filt_pipewrite(struct knote *kn, long hint)
{
	struct pipe *rpipe = (struct pipe *)kn->kn_fp->f_data;
	struct pipe *wpipe = rpipe->pipe_peer;
	u_int32_t space;

	if ((wpipe == NULL) || (wpipe->pipe_state & PIPE_EOF)) {
		kn->kn_data = 0;
		kn->kn_flags |= EV_EOF; 
		return (1);
	}
	space = wpipe->pipe_buffer.windex -
		wpipe->pipe_buffer.rindex;
	space = wpipe->pipe_buffer.size - space;
	kn->kn_data = space;
	return (kn->kn_data >= PIPE_BUF);
}
