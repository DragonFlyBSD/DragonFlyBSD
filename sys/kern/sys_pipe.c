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
 * $DragonFly: src/sys/kern/sys_pipe.c,v 1.18 2004/05/01 18:16:43 dillon Exp $
 */

/*
 * This file contains a high-performance replacement for the socket-based
 * pipes scheme originally used in FreeBSD/4.4Lite.  It does not support
 * all features of sockets, but does do everything that pipes normally
 * do.
 */

/*
 * This code has two modes of operation, a small write mode and a large
 * write mode.  The small write mode acts like conventional pipes with
 * a kernel buffer.  If the buffer is less than PIPE_MINDIRECT, then the
 * "normal" pipe buffering is done.  If the buffer is between PIPE_MINDIRECT
 * and PIPE_SIZE in size, it is fully mapped and wired into the kernel, and
 * the receiving process can copy it directly from the pages in the sending
 * process.
 *
 * If the sending process receives a signal, it is possible that it will
 * go away, and certainly its address space can change, because control
 * is returned back to the user-mode side.  In that case, the pipe code
 * arranges to copy the buffer supplied by the user process, to a pageable
 * kernel buffer, and the receiving process will grab the data from the
 * pageable kernel buffer.  Since signals don't happen all that often,
 * the copy operation is normally eliminated.
 *
 * The constant PIPE_MINDIRECT is chosen to make sure that buffering will
 * happen for small transfers so that the system will not spend all of
 * its time context switching.  PIPE_SIZE is constrained by the
 * amount of kernel virtual memory.
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
		struct ucred *cred, int flags, struct thread *td);
static int pipe_write (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td);
static int pipe_close (struct file *fp, struct thread *td);
static int pipe_poll (struct file *fp, int events, struct ucred *cred,
		struct thread *td);
static int pipe_kqfilter (struct file *fp, struct knote *kn);
static int pipe_stat (struct file *fp, struct stat *sb, struct thread *td);
static int pipe_ioctl (struct file *fp, u_long cmd, caddr_t data, struct thread *td);

static struct fileops pipeops = {
	NULL,	/* port */
	0,	/* autoq */
	pipe_read, pipe_write, pipe_ioctl, pipe_poll, pipe_kqfilter,
	pipe_stat, pipe_close
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
 * Maximum amount of kva for pipes -- this is kind-of a soft limit, but
 * is there so that on large systems, we don't exhaust it.
 */
#define MAXPIPEKVA (8*1024*1024)

/*
 * Limit for direct transfers, we cannot, of course limit
 * the amount of kva for pipes in general though.
 */
#define LIMITPIPEKVA (16*1024*1024)

/*
 * Limit the number of "big" pipes
 */
#define LIMITBIGPIPES	32
#define PIPEQ_MAX_CACHE 16      /* per-cpu pipe structure cache */

static int pipe_maxbig = LIMITBIGPIPES;
static int pipe_maxcache = PIPEQ_MAX_CACHE;
static int pipe_nbig;
static int pipe_bcache_alloc;
static int pipe_bkmem_alloc;
static int pipe_dwrite_enable = 1;	/* 0:copy, 1:kmem/sfbuf 2:force */
static int pipe_dwrite_sfbuf = 1;	/* 0:kmem_map 1:sfbufs 2:sfbufs_dmap */
					/* 3:sfbuf_dmap w/ forced invlpg */

SYSCTL_NODE(_kern, OID_AUTO, pipe, CTLFLAG_RW, 0, "Pipe operation");
SYSCTL_INT(_kern_pipe, OID_AUTO, nbig,
        CTLFLAG_RD, &pipe_nbig, 0, "numer of big pipes allocated");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxcache,
        CTLFLAG_RW, &pipe_maxcache, 0, "max pipes cached per-cpu");
SYSCTL_INT(_kern_pipe, OID_AUTO, maxbig,
        CTLFLAG_RW, &pipe_maxbig, 0, "max number of big pipes");
SYSCTL_INT(_kern_pipe, OID_AUTO, dwrite_enable,
        CTLFLAG_RW, &pipe_dwrite_enable, 0, "1:enable/2:force direct writes");
SYSCTL_INT(_kern_pipe, OID_AUTO, dwrite_sfbuf,
        CTLFLAG_RW, &pipe_dwrite_sfbuf, 0, "(if dwrite_enable) 0:kmem 1:sfbuf 2:sfbuf_dmap 3:sfbuf_dmap_forceinvlpg");
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
#ifndef PIPE_NODIRECT
static int pipe_build_write_buffer (struct pipe *wpipe, struct uio *uio);
static int pipe_direct_write (struct pipe *wpipe, struct uio *uio);
static void pipe_clone_write_buffer (struct pipe *wpipe);
#endif
static int pipespace (struct pipe *cpipe, int size);

/*
 * The pipe system call for the DTYPE_PIPE type of pipes
 *
 * pipe_ARgs(int dummy)
 */

/* ARGSUSED */
int
pipe(struct pipe_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *rf, *wf;
	struct pipe *rpipe, *wpipe;
	int fd1, fd2, error;

	KKASSERT(p);
	fdp = p->p_fd;

	rpipe = wpipe = NULL;
	if (pipe_create(&rpipe) || pipe_create(&wpipe)) {
		pipeclose(rpipe); 
		pipeclose(wpipe); 
		return (ENFILE);
	}
	
	rpipe->pipe_state |= PIPE_DIRECTOK;
	wpipe->pipe_state |= PIPE_DIRECTOK;

	/*
	 * Select the direct-map features to use for this pipe.  Since the
	 * sysctl's can change on the fly we record the settings when the
	 * pipe is created.
	 *
	 * Generally speaking the system will default to what we consider
	 * to be the best-balanced and most stable option.  Right now this
	 * is SFBUF1.  Modes 2 and 3 are considered experiemental at the
	 * moment.
	 */
	wpipe->pipe_feature = PIPE_COPY;
	if (pipe_dwrite_enable) {
		switch(pipe_dwrite_sfbuf) {
		case 0:
			wpipe->pipe_feature = PIPE_KMEM;
			break;
		case 1:
			wpipe->pipe_feature = PIPE_SFBUF1;
			break;
		case 2:
		case 3:
			wpipe->pipe_feature = PIPE_SFBUF2;
			break;
		}
	}
	rpipe->pipe_feature = wpipe->pipe_feature;

	error = falloc(p, &rf, &fd1);
	if (error) {
		pipeclose(rpipe);
		pipeclose(wpipe);
		return (error);
	}
	fhold(rf);
	uap->sysmsg_fds[0] = fd1;

	/*
	 * Warning: once we've gotten past allocation of the fd for the
	 * read-side, we can only drop the read side via fdrop() in order
	 * to avoid races against processes which manage to dup() the read
	 * side while we are blocked trying to allocate the write side.
	 */
	rf->f_flag = FREAD | FWRITE;
	rf->f_type = DTYPE_PIPE;
	rf->f_data = (caddr_t)rpipe;
	rf->f_ops = &pipeops;
	error = falloc(p, &wf, &fd2);
	if (error) {
		if (fdp->fd_ofiles[fd1] == rf) {
			fdp->fd_ofiles[fd1] = NULL;
			fdrop(rf, td);
		}
		fdrop(rf, td);
		/* rpipe has been closed by fdrop(). */
		pipeclose(wpipe);
		return (error);
	}
	wf->f_flag = FREAD | FWRITE;
	wf->f_type = DTYPE_PIPE;
	wf->f_data = (caddr_t)wpipe;
	wf->f_ops = &pipeops;
	uap->sysmsg_fds[1] = fd2;

	rpipe->pipe_peer = wpipe;
	wpipe->pipe_peer = rpipe;
	fdrop(rf, td);

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
		buffer = (caddr_t) vm_map_min(kernel_map);

		error = vm_map_find(kernel_map, object, 0,
			(vm_offset_t *) &buffer, size, 1,
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
	cpipe->pipe_buffer.in = 0;
	cpipe->pipe_buffer.out = 0;
	cpipe->pipe_buffer.cnt = 0;
	return (0);
}

/*
 * Initialize and allocate VM and memory for pipe, pulling the pipe from
 * our per-cpu cache if possible.  For now make sure it is sized for the
 * smaller PIPE_SIZE default.
 */
static int
pipe_create(cpipep)
	struct pipe **cpipep;
{
	globaldata_t gd = mycpu;
	struct pipe *cpipe;
	int error;

	if ((cpipe = gd->gd_pipeq) != NULL) {
		gd->gd_pipeq = cpipe->pipe_peer;
		--gd->gd_pipeqcount;
		cpipe->pipe_peer = NULL;
	} else {
		cpipe = malloc(sizeof(struct pipe), M_PIPE, M_WAITOK|M_ZERO);
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
pipelock(cpipe, catch)
	struct pipe *cpipe;
	int catch;
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
pipeunlock(cpipe)
	struct pipe *cpipe;
{

	cpipe->pipe_state &= ~PIPE_LOCK;
	if (cpipe->pipe_state & PIPE_LWANT) {
		cpipe->pipe_state &= ~PIPE_LWANT;
		wakeup(cpipe);
	}
}

static __inline void
pipeselwakeup(cpipe)
	struct pipe *cpipe;
{

	if (cpipe->pipe_state & PIPE_SEL) {
		cpipe->pipe_state &= ~PIPE_SEL;
		selwakeup(&cpipe->pipe_sel);
	}
	if ((cpipe->pipe_state & PIPE_ASYNC) && cpipe->pipe_sigio)
		pgsigio(cpipe->pipe_sigio, SIGIO, 0);
	KNOTE(&cpipe->pipe_sel.si_note, 0);
}

/* ARGSUSED */
static int
pipe_read(struct file *fp, struct uio *uio, struct ucred *cred,
	int flags, struct thread *td)
{
	struct pipe *rpipe = (struct pipe *) fp->f_data;
	int error;
	int nread = 0;
	u_int size;

	++rpipe->pipe_busy;
	error = pipelock(rpipe, 1);
	if (error)
		goto unlocked_error;

	while (uio->uio_resid) {
		caddr_t va;

		if (rpipe->pipe_buffer.cnt > 0) {
			/*
			 * normal pipe buffer receive
			 */
			size = rpipe->pipe_buffer.size - rpipe->pipe_buffer.out;
			if (size > rpipe->pipe_buffer.cnt)
				size = rpipe->pipe_buffer.cnt;
			if (size > (u_int) uio->uio_resid)
				size = (u_int) uio->uio_resid;

			error = uiomove(&rpipe->pipe_buffer.buffer[rpipe->pipe_buffer.out],
					size, uio);
			if (error)
				break;

			rpipe->pipe_buffer.out += size;
			if (rpipe->pipe_buffer.out >= rpipe->pipe_buffer.size)
				rpipe->pipe_buffer.out = 0;

			rpipe->pipe_buffer.cnt -= size;

			/*
			 * If there is no more to read in the pipe, reset
			 * its pointers to the beginning.  This improves
			 * cache hit stats.
			 */
			if (rpipe->pipe_buffer.cnt == 0) {
				rpipe->pipe_buffer.in = 0;
				rpipe->pipe_buffer.out = 0;
			}
			nread += size;
#ifndef PIPE_NODIRECT
		} else if (rpipe->pipe_kva &&
			   rpipe->pipe_feature == PIPE_KMEM &&
			   (rpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) 
			       == PIPE_DIRECTW
		) {
			/*
			 * Direct copy using source-side kva mapping
			 */
			size = rpipe->pipe_map.xio_bytes;
			if (size > (u_int)uio->uio_resid)
				size = (u_int)uio->uio_resid;
			va = (caddr_t)rpipe->pipe_kva + rpipe->pipe_map.xio_offset;
			error = uiomove(va, size, uio);
			if (error)
				break;
			nread += size;
			rpipe->pipe_map.xio_offset += size;
			rpipe->pipe_map.xio_bytes -= size;
			if (rpipe->pipe_map.xio_bytes == 0) {
				rpipe->pipe_state |= PIPE_DIRECTIP;
				rpipe->pipe_state &= ~PIPE_DIRECTW;
				wakeup(rpipe);
			}
		} else if (rpipe->pipe_map.xio_bytes &&
			   rpipe->pipe_kva &&
			   rpipe->pipe_feature == PIPE_SFBUF2 &&
			   (rpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) 
			       == PIPE_DIRECTW
		) {
			/*
			 * Direct copy, bypassing a kernel buffer.  We cannot
			 * mess with the direct-write buffer until
			 * PIPE_DIRECTIP is cleared.  In order to prevent 
			 * the pipe_write code from racing itself in
			 * direct_write, we set DIRECTIP when we clear
			 * DIRECTW after we have exhausted the buffer.
			 */
			if (pipe_dwrite_sfbuf == 3)
				rpipe->pipe_kvamask = 0;
			pmap_qenter2(rpipe->pipe_kva, rpipe->pipe_map.xio_pages,
				    rpipe->pipe_map.xio_npages,
				    &rpipe->pipe_kvamask);
			size = rpipe->pipe_map.xio_bytes;
			if (size > (u_int)uio->uio_resid)
				size = (u_int)uio->uio_resid;
			va = (caddr_t)rpipe->pipe_kva + 
				rpipe->pipe_map.xio_offset;
			error = uiomove(va, size, uio);
			if (error)
				break;
			nread += size;
			rpipe->pipe_map.xio_offset += size;
			rpipe->pipe_map.xio_bytes -= size;
			if (rpipe->pipe_map.xio_bytes == 0) {
				rpipe->pipe_state |= PIPE_DIRECTIP;
				rpipe->pipe_state &= ~PIPE_DIRECTW;
				wakeup(rpipe);
			}
		} else if (rpipe->pipe_map.xio_bytes &&
			   rpipe->pipe_feature == PIPE_SFBUF1 &&
			   (rpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) 
				== PIPE_DIRECTW
		) {
			/*
			 * Direct copy, bypassing a kernel buffer.  We cannot
			 * mess with the direct-write buffer until
			 * PIPE_DIRECTIP is cleared.  In order to prevent 
			 * the pipe_write code from racing itself in
			 * direct_write, we set DIRECTIP when we clear
			 * DIRECTW after we have exhausted the buffer.
			 */
			error = xio_uio_copy(&rpipe->pipe_map, uio, &size);
			if (error)
				break;
			nread += size;
			if (rpipe->pipe_map.xio_bytes == 0) {
				rpipe->pipe_state |= PIPE_DIRECTIP;
				rpipe->pipe_state &= ~PIPE_DIRECTW;
				wakeup(rpipe);
			}
#endif
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
			if (fp->f_flag & FNONBLOCK) {
				error = EAGAIN;
			} else {
				rpipe->pipe_state |= PIPE_WANTR;
				if ((error = tsleep(rpipe, PCATCH|PNORESCHED,
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
	if ((rpipe->pipe_busy == 0) && (rpipe->pipe_state & PIPE_WANT)) {
		rpipe->pipe_state &= ~(PIPE_WANT|PIPE_WANTW);
		wakeup(rpipe);
	} else if (rpipe->pipe_buffer.cnt < MINPIPESIZE) {
		/*
		 * Handle write blocking hysteresis.
		 */
		if (rpipe->pipe_state & PIPE_WANTW) {
			rpipe->pipe_state &= ~PIPE_WANTW;
			wakeup(rpipe);
		}
	}

	if ((rpipe->pipe_buffer.size - rpipe->pipe_buffer.cnt) >= PIPE_BUF)
		pipeselwakeup(rpipe);
	return (error);
}

#ifndef PIPE_NODIRECT
/*
 * Map the sending processes' buffer into kernel space and wire it.
 * This is similar to a physical write operation.
 */
static int
pipe_build_write_buffer(wpipe, uio)
	struct pipe *wpipe;
	struct uio *uio;
{
	int error;
	u_int size;

	size = (u_int) uio->uio_iov->iov_len;
	if (size > wpipe->pipe_buffer.size)
		size = wpipe->pipe_buffer.size;
	if (size > XIO_INTERNAL_SIZE)
		size = XIO_INTERNAL_SIZE;

	error = xio_init_ubuf(&wpipe->pipe_map, uio->uio_iov->iov_base, 
				size, XIOF_READ);
	if (error)
		return(error);

	/*
	 * Create a kernel map for KMEM and SFBUF2 copy modes.  SFBUF2 will
	 * map the pages on the target while KMEM maps the pages now.
	 */
	switch(wpipe->pipe_feature) {
	case PIPE_KMEM:
	case PIPE_SFBUF2:
		if (wpipe->pipe_kva == NULL) {
			wpipe->pipe_kva = 
			    kmem_alloc_pageable(kernel_map, XIO_INTERNAL_SIZE);
			wpipe->pipe_kvamask = 0;
		}
		if (wpipe->pipe_feature == PIPE_KMEM) {
			pmap_qenter(wpipe->pipe_kva, wpipe->pipe_map.xio_pages,
				    wpipe->pipe_map.xio_npages);
		}
		break;
	default:
		break;
	}

	/*
	 * and update the uio data
	 */
	uio->uio_iov->iov_len -= size;
	uio->uio_iov->iov_base += size;
	if (uio->uio_iov->iov_len == 0)
		uio->uio_iov++;
	uio->uio_resid -= size;
	uio->uio_offset += size;
	return (0);
}

/*
 * In the case of a signal, the writing process might go away.  This
 * code copies the data into the circular buffer so that the source
 * pages can be freed without loss of data.
 */
static void
pipe_clone_write_buffer(wpipe)
	struct pipe *wpipe;
{
	int size;

	size = wpipe->pipe_map.xio_bytes;

	wpipe->pipe_buffer.in = size;
	wpipe->pipe_buffer.out = 0;
	wpipe->pipe_buffer.cnt = size;
	wpipe->pipe_state &= ~(PIPE_DIRECTW | PIPE_DIRECTIP);

	xio_copy_xtok(&wpipe->pipe_map, wpipe->pipe_buffer.buffer, size);
	xio_release(&wpipe->pipe_map);
	if (wpipe->pipe_kva) {
		kmem_free(kernel_map, wpipe->pipe_kva, XIO_INTERNAL_SIZE);
		wpipe->pipe_kva = NULL;
	}

}

/*
 * This implements the pipe buffer write mechanism.  Note that only
 * a direct write OR a normal pipe write can be pending at any given time.
 * If there are any characters in the pipe buffer, the direct write will
 * be deferred until the receiving process grabs all of the bytes from
 * the pipe buffer.  Then the direct mapping write is set-up.
 */
static int
pipe_direct_write(wpipe, uio)
	struct pipe *wpipe;
	struct uio *uio;
{
	int error;

retry:
	while (wpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) {
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}
		wpipe->pipe_state |= PIPE_WANTW;
		error = tsleep(wpipe, PCATCH, "pipdww", 0);
		if (error)
			goto error2;
		if (wpipe->pipe_state & PIPE_EOF) {
			error = EPIPE;
			goto error2;
		}
	}
	KKASSERT(wpipe->pipe_map.xio_bytes == 0);
	if (wpipe->pipe_buffer.cnt > 0) {
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}
			
		wpipe->pipe_state |= PIPE_WANTW;
		error = tsleep(wpipe, PCATCH, "pipdwc", 0);
		if (error)
			goto error2;
		if (wpipe->pipe_state & PIPE_EOF) {
			error = EPIPE;
			goto error2;
		}
		goto retry;
	}

	/*
	 * Build our direct-write buffer
	 */
	wpipe->pipe_state |= PIPE_DIRECTW | PIPE_DIRECTIP;
	error = pipe_build_write_buffer(wpipe, uio);
	if (error)
		goto error1;
	wpipe->pipe_state &= ~PIPE_DIRECTIP;

	/*
	 * Wait until the receiver has snarfed the data.  Since we are likely
	 * going to sleep we optimize the case and yield synchronously,
	 * possibly avoiding the tsleep().
	 */
	error = 0;
	while (!error && (wpipe->pipe_state & PIPE_DIRECTW)) {
		if (wpipe->pipe_state & PIPE_EOF) {
			pipelock(wpipe, 0);
			xio_release(&wpipe->pipe_map);
			if (wpipe->pipe_kva) {
				kmem_free(kernel_map, wpipe->pipe_kva, XIO_INTERNAL_SIZE);
				wpipe->pipe_kva = NULL;
			}
			pipeunlock(wpipe);
			pipeselwakeup(wpipe);
			error = EPIPE;
			goto error1;
		}
		if (wpipe->pipe_state & PIPE_WANTR) {
			wpipe->pipe_state &= ~PIPE_WANTR;
			wakeup(wpipe);
		}
		pipeselwakeup(wpipe);
		error = tsleep(wpipe, PCATCH|PNORESCHED, "pipdwt", 0);
	}
	pipelock(wpipe,0);
	if (wpipe->pipe_state & PIPE_DIRECTW) {
		/*
		 * this bit of trickery substitutes a kernel buffer for
		 * the process that might be going away.
		 */
		pipe_clone_write_buffer(wpipe);
		KKASSERT((wpipe->pipe_state & PIPE_DIRECTIP) == 0);
	} else {
		KKASSERT(wpipe->pipe_state & PIPE_DIRECTIP);
		xio_release(&wpipe->pipe_map);
		wpipe->pipe_state &= ~PIPE_DIRECTIP;
	}
	pipeunlock(wpipe);
	return (error);

	/*
	 * Direct-write error, clear the direct write flags.
	 */
error1:
	wpipe->pipe_state &= ~(PIPE_DIRECTW | PIPE_DIRECTIP);
	/* fallthrough */

	/*
	 * General error, wakeup the other side if it happens to be sleeping.
	 */
error2:
	wakeup(wpipe);
	return (error);
}
#endif
	
static int
pipe_write(struct file *fp, struct uio *uio, struct ucred *cred,
	int flags, struct thread *td)
{
	int error = 0;
	int orig_resid;
	struct pipe *wpipe, *rpipe;

	rpipe = (struct pipe *) fp->f_data;
	wpipe = rpipe->pipe_peer;

	/*
	 * detect loss of pipe read side, issue SIGPIPE if lost.
	 */
	if ((wpipe == NULL) || (wpipe->pipe_state & PIPE_EOF)) {
		return (EPIPE);
	}
	++wpipe->pipe_busy;

	/*
	 * If it is advantageous to resize the pipe buffer, do
	 * so.
	 */
	if ((uio->uio_resid > PIPE_SIZE) &&
		(pipe_nbig < pipe_maxbig) &&
		(wpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) == 0 &&
		(wpipe->pipe_buffer.size <= PIPE_SIZE) &&
		(wpipe->pipe_buffer.cnt == 0)) {

		if ((error = pipelock(wpipe,1)) == 0) {
			if (pipespace(wpipe, BIG_PIPE_SIZE) == 0)
				pipe_nbig++;
			pipeunlock(wpipe);
		}
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
		return(error);
	}
		
	KASSERT(wpipe->pipe_buffer.buffer != NULL, ("pipe buffer gone"));

	orig_resid = uio->uio_resid;

	while (uio->uio_resid) {
		int space;

#ifndef PIPE_NODIRECT
		/*
		 * If the transfer is large, we can gain performance if
		 * we do process-to-process copies directly.
		 * If the write is non-blocking, we don't use the
		 * direct write mechanism.
		 *
		 * The direct write mechanism will detect the reader going
		 * away on us.
		 */
		if ((uio->uio_iov->iov_len >= PIPE_MINDIRECT ||
		    pipe_dwrite_enable > 1) &&
		    (fp->f_flag & FNONBLOCK) == 0 &&
		    pipe_dwrite_enable) {
			error = pipe_direct_write( wpipe, uio);
			if (error)
				break;
			continue;
		}
#endif

		/*
		 * Pipe buffered writes cannot be coincidental with
		 * direct writes.  We wait until the currently executing
		 * direct write is completed before we start filling the
		 * pipe buffer.  We break out if a signal occurs or the
		 * reader goes away.
		 */
	retrywrite:
		while (wpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) {
			if (wpipe->pipe_state & PIPE_WANTR) {
				wpipe->pipe_state &= ~PIPE_WANTR;
				wakeup(wpipe);
			}
			error = tsleep(wpipe, PCATCH, "pipbww", 0);
			if (wpipe->pipe_state & PIPE_EOF)
				break;
			if (error)
				break;
		}
		if (wpipe->pipe_state & PIPE_EOF) {
			error = EPIPE;
			break;
		}

		space = wpipe->pipe_buffer.size - wpipe->pipe_buffer.cnt;

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
				int size;	/* Transfer size */
				int segsize;	/* first segment to transfer */

				/*
				 * It is possible for a direct write to
				 * slip in on us... handle it here...
				 */
				if (wpipe->pipe_state & (PIPE_DIRECTW|PIPE_DIRECTIP)) {
					pipeunlock(wpipe);
					goto retrywrite;
				}
				/* 
				 * If a process blocked in uiomove, our
				 * value for space might be bad.
				 *
				 * XXX will we be ok if the reader has gone
				 * away here?
				 */
				if (space > wpipe->pipe_buffer.size - 
				    wpipe->pipe_buffer.cnt) {
					pipeunlock(wpipe);
					goto retrywrite;
				}

				/*
				 * Transfer size is minimum of uio transfer
				 * and free space in pipe buffer.
				 */
				if (space > uio->uio_resid)
					size = uio->uio_resid;
				else
					size = space;
				/*
				 * First segment to transfer is minimum of 
				 * transfer size and contiguous space in
				 * pipe buffer.  If first segment to transfer
				 * is less than the transfer size, we've got
				 * a wraparound in the buffer.
				 */
				segsize = wpipe->pipe_buffer.size - 
					wpipe->pipe_buffer.in;
				if (segsize > size)
					segsize = size;
				
				/* Transfer first segment */

				error = uiomove(&wpipe->pipe_buffer.buffer[wpipe->pipe_buffer.in], 
						segsize, uio);
				
				if (error == 0 && segsize < size) {
					/* 
					 * Transfer remaining part now, to
					 * support atomic writes.  Wraparound
					 * happened.
					 */
					if (wpipe->pipe_buffer.in + segsize != 
					    wpipe->pipe_buffer.size)
						panic("Expected pipe buffer wraparound disappeared");
						
					error = uiomove(&wpipe->pipe_buffer.buffer[0],
							size - segsize, uio);
				}
				if (error == 0) {
					wpipe->pipe_buffer.in += size;
					if (wpipe->pipe_buffer.in >=
					    wpipe->pipe_buffer.size) {
						if (wpipe->pipe_buffer.in != size - segsize + wpipe->pipe_buffer.size)
							panic("Expected wraparound bad");
						wpipe->pipe_buffer.in = size - segsize;
					}
				
					wpipe->pipe_buffer.cnt += size;
					if (wpipe->pipe_buffer.cnt > wpipe->pipe_buffer.size)
						panic("Pipe buffer overflow");
				
				}
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
			if (fp->f_flag & FNONBLOCK) {
				error = EAGAIN;
				break;
			}

			/*
			 * We have no more space and have something to offer,
			 * wake up select/poll.
			 */
			pipeselwakeup(wpipe);

			wpipe->pipe_state |= PIPE_WANTW;
			error = tsleep(wpipe, PCATCH|PNORESCHED, "pipewr", 0);
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
	} else if (wpipe->pipe_buffer.cnt > 0) {
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
	if ((wpipe->pipe_buffer.cnt == 0) &&
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
	if (wpipe->pipe_buffer.cnt)
		pipeselwakeup(wpipe);

	return (error);
}

/*
 * we implement a very minimal set of ioctls for compatibility with sockets.
 */
int
pipe_ioctl(struct file *fp, u_long cmd, caddr_t data, struct thread *td)
{
	struct pipe *mpipe = (struct pipe *)fp->f_data;

	switch (cmd) {

	case FIONBIO:
		return (0);

	case FIOASYNC:
		if (*(int *)data) {
			mpipe->pipe_state |= PIPE_ASYNC;
		} else {
			mpipe->pipe_state &= ~PIPE_ASYNC;
		}
		return (0);

	case FIONREAD:
		if (mpipe->pipe_state & PIPE_DIRECTW) {
			*(int *)data = mpipe->pipe_map.xio_bytes;
		} else {
			*(int *)data = mpipe->pipe_buffer.cnt;
		}
		return (0);

	case FIOSETOWN:
		return (fsetown(*(int *)data, &mpipe->pipe_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(mpipe->pipe_sigio);
		return (0);

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		return (fsetown(-(*(int *)data), &mpipe->pipe_sigio));

	/* This is deprecated, FIOGETOWN should be used instead. */
	case TIOCGPGRP:
		*(int *)data = -fgetown(mpipe->pipe_sigio);
		return (0);

	}
	return (ENOTTY);
}

int
pipe_poll(struct file *fp, int events, struct ucred *cred, struct thread *td)
{
	struct pipe *rpipe = (struct pipe *)fp->f_data;
	struct pipe *wpipe;
	int revents = 0;

	wpipe = rpipe->pipe_peer;
	if (events & (POLLIN | POLLRDNORM))
		if ((rpipe->pipe_state & PIPE_DIRECTW) ||
		    (rpipe->pipe_buffer.cnt > 0) ||
		    (rpipe->pipe_state & PIPE_EOF))
			revents |= events & (POLLIN | POLLRDNORM);

	if (events & (POLLOUT | POLLWRNORM))
		if (wpipe == NULL || (wpipe->pipe_state & PIPE_EOF) ||
		    (((wpipe->pipe_state & PIPE_DIRECTW) == 0) &&
		     (wpipe->pipe_buffer.size - wpipe->pipe_buffer.cnt) >= PIPE_BUF))
			revents |= events & (POLLOUT | POLLWRNORM);

	if ((rpipe->pipe_state & PIPE_EOF) ||
	    (wpipe == NULL) ||
	    (wpipe->pipe_state & PIPE_EOF))
		revents |= POLLHUP;

	if (revents == 0) {
		if (events & (POLLIN | POLLRDNORM)) {
			selrecord(td, &rpipe->pipe_sel);
			rpipe->pipe_state |= PIPE_SEL;
		}

		if (events & (POLLOUT | POLLWRNORM)) {
			selrecord(td, &wpipe->pipe_sel);
			wpipe->pipe_state |= PIPE_SEL;
		}
	}

	return (revents);
}

static int
pipe_stat(struct file *fp, struct stat *ub, struct thread *td)
{
	struct pipe *pipe = (struct pipe *)fp->f_data;

	bzero((caddr_t)ub, sizeof(*ub));
	ub->st_mode = S_IFIFO;
	ub->st_blksize = pipe->pipe_buffer.size;
	ub->st_size = pipe->pipe_buffer.cnt;
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

/* ARGSUSED */
static int
pipe_close(struct file *fp, struct thread *td)
{
	struct pipe *cpipe = (struct pipe *)fp->f_data;

	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	funsetown(cpipe->pipe_sigio);
	pipeclose(cpipe);
	return (0);
}

static void
pipe_free_kmem(struct pipe *cpipe)
{
	if (cpipe->pipe_buffer.buffer != NULL) {
		if (cpipe->pipe_buffer.size > PIPE_SIZE)
			--pipe_nbig;
		kmem_free(kernel_map,
			(vm_offset_t)cpipe->pipe_buffer.buffer,
			cpipe->pipe_buffer.size);
		cpipe->pipe_buffer.buffer = NULL;
		cpipe->pipe_buffer.object = NULL;
	}
#ifndef PIPE_NODIRECT
	KKASSERT(cpipe->pipe_map.xio_bytes == 0 &&
		cpipe->pipe_map.xio_offset == 0 &&
		cpipe->pipe_map.xio_npages == 0);
#endif
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
		kmem_free(kernel_map, cpipe->pipe_kva, XIO_INTERNAL_SIZE);
		cpipe->pipe_kva = NULL;
	}

	/*
	 * free or cache resources
	 */
	gd = mycpu;
	if (gd->gd_pipeqcount >= pipe_maxcache ||
	    cpipe->pipe_buffer.size != PIPE_SIZE
	) {
		pipe_free_kmem(cpipe);
		free(cpipe, M_PIPE);
	} else {
		KKASSERT(cpipe->pipe_map.xio_npages == 0 &&
			cpipe->pipe_map.xio_bytes == 0 &&
			cpipe->pipe_map.xio_offset == 0);
		cpipe->pipe_state = 0;
		cpipe->pipe_busy = 0;
		cpipe->pipe_peer = gd->gd_pipeq;
		gd->gd_pipeq = cpipe;
		++gd->gd_pipeqcount;
	}
}

/*ARGSUSED*/
static int
pipe_kqfilter(struct file *fp, struct knote *kn)
{
	struct pipe *cpipe = (struct pipe *)kn->kn_fp->f_data;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &pipe_rfiltops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &pipe_wfiltops;
		cpipe = cpipe->pipe_peer;
		if (cpipe == NULL)
			/* other end of pipe has been closed */
			return (EPIPE);
		break;
	default:
		return (1);
	}
	kn->kn_hook = (caddr_t)cpipe;

	SLIST_INSERT_HEAD(&cpipe->pipe_sel.si_note, kn, kn_selnext);
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

	kn->kn_data = rpipe->pipe_buffer.cnt;
	if ((kn->kn_data == 0) && (rpipe->pipe_state & PIPE_DIRECTW))
		kn->kn_data = rpipe->pipe_map.xio_bytes;

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

	if ((wpipe == NULL) || (wpipe->pipe_state & PIPE_EOF)) {
		kn->kn_data = 0;
		kn->kn_flags |= EV_EOF; 
		return (1);
	}
	kn->kn_data = wpipe->pipe_buffer.size - wpipe->pipe_buffer.cnt;
	if (wpipe->pipe_state & PIPE_DIRECTW)
		kn->kn_data = 0;

	return (kn->kn_data >= PIPE_BUF);
}
