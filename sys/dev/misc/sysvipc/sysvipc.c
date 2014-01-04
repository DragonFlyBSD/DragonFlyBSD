/*
 * Copyright (c) 2013 Larisa Grigore  <larisagrigore@gmail.com>.
 * All rights reserved.
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysmsg.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/mman.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <machine/varargs.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/spinlock2.h>

#include <sys/sysvipc.h>
#include <sys/shm.h>

#include <vm/vm.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>
#include <vm/vm_map.h>

static cdev_t		sysvipc_dev;
static d_open_t		sysvipc_dev_open;
static d_close_t	sysvipc_dev_close;
//static d_read_t		sysvipc_dev_read;
//static d_write_t	sysvipc_dev_write;
static d_ioctl_t	sysvipc_dev_ioctl;
static d_kqfilter_t	sysvipc_dev_kqfilter;

static struct dev_ops sysvipc_dev_ops = {
	{ "sysv", 0, 0 },
	.d_open =	sysvipc_dev_open,
	.d_close =	sysvipc_dev_close,
	.d_ioctl =	sysvipc_dev_ioctl,
	.d_kqfilter =	sysvipc_dev_kqfilter,
};

struct sysvipc_req {
	struct proc			*proc;
	int				fd[2];
	TAILQ_ENTRY(sysvipc_req)	req_link;
};

TAILQ_HEAD(_req_list, sysvipc_req);

struct sysvipc_softc {
	cdev_t			sysvipc_dev;
	struct kqinfo		sysvipc_rkq;
	int			opened;
	struct _req_list	consumed_list;
	struct _req_list	req_list;
	struct lock		consumed_mtx;
	struct lock		req_mtx;
	struct thread		*sysvipc_daemon_thread;
};

static int
sysvipc_register(cdev_t dev)
{
	struct sysvipc_softc *sysv;
	
	if (dev->si_drv1 != NULL)
		return (EEXIST);

	kprintf("aloc sysv\n");
	dev->si_drv1 = sysv = (struct sysvipc_softc *)
		kmalloc(sizeof(*sysv), M_TEMP,
				M_ZERO | M_WAITOK);
	sysv->sysvipc_dev = dev;
	TAILQ_INIT(&sysv->req_list);
	TAILQ_INIT(&sysv->consumed_list);
	lockinit(&sysv->req_mtx, "sysvlkr", 0, LK_CANRECURSE);
	lockinit(&sysv->consumed_mtx, "sysvlkc", 0, LK_CANRECURSE);
	sysv->sysvipc_daemon_thread = curthread;

	return 0;
}

static int
sysvipc_unregister(cdev_t dev)
{
	struct sysvipc_softc *sysv = dev->si_drv1;

	kfree(sysv, M_TEMP);
	dev->si_drv1 = NULL;

	return 0;
}

/*
 * dev stuff
 */
static int
sysvipc_dev_open(struct dev_open_args *ap)
{
	return 0;
}

static int
sysvipc_dev_close(struct dev_close_args *ap)
{
	return 0;
}

static void filt_sysvipc_detach(struct knote *);
static int filt_sysvipc_read(struct knote *, long);

static struct filterops sysvipc_read_filtops =
	{ FILTEROP_ISFD, NULL, filt_sysvipc_detach, filt_sysvipc_read };

static int
sysvipc_dev_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct sysvipc_softc *sysv = dev->si_drv1;
	struct klist *list;

	if(sysv==NULL){
		kprintf("error read\n");
		return -1;
	}
	kprintf("kqfilter\n");

	sysv = dev->si_drv1;
	list = &sysv->sysvipc_rkq.ki_note;
	ap->a_result =0;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kprintf("event read\n");
		kn->kn_fop = &sysvipc_read_filtops;
		kn->kn_hook = (void *)sysv;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return(0);
	}

	knote_insert(list, kn);
	return(0);

}

/* TODO: kernel panic at line 181. it is called after unregister_daemon.*/
static void
filt_sysvipc_detach(struct knote *kn)
{
	/*kprintf("detach\n");
	if(!kn)
		return;
	kprintf("detach 1\n");

	struct sysvipc_softc * sysv =
		(struct sysvipc_softc *)kn->kn_hook;

	if(!sysv)
		return;
	kprintf("detach 2\n");

	knote_remove(&sysv->sysvipc_rkq.ki_note, kn);
	kprintf("end detach\n");
*/
}

static int
filt_sysvipc_read(struct knote *kn, long hint)
{
	struct sysvipc_softc * sysv =
		(struct sysvipc_softc *)kn->kn_hook;
	int ready = 0;

	//TODO what type of lock should I use?!?!
	lockmgr(&sysv->req_mtx, LK_EXCLUSIVE);
	ready = !(TAILQ_EMPTY(&sysv->req_list));
	lockmgr(&sysv->req_mtx, LK_RELEASE);
	return (ready);
}

static int
sysvipc_install_fd(struct proc *p_from, struct proc *p_to, int fd)
{
	struct filedesc *fdp_from = p_from->p_fd;
	struct filedesc *fdp_to = p_to->p_fd;
	
	struct file *fp;
	int error, newfd;
	int flags;

	/*
	 * Get the file corresponding to fd from the process p_from.
	 */
	spin_lock(&fdp_from->fd_spin);
	if ((unsigned)fd >= fdp_from->fd_nfiles || fdp_from->fd_files[fd].fp == NULL) {
		spin_unlock(&fdp_from->fd_spin);
		return (EBADF);
	}

	fp = fdp_from->fd_files[fd].fp;
	flags = fdp_from->fd_files[fd].fileflags;
	fhold(fp);	/* MPSAFE - can be called with a spinlock held */
	spin_unlock(&fdp_from->fd_spin);

	/*
	 * Reserve a fd in the process p_to.
	 */
	error = fdalloc(p_to, 1, &newfd);
	if (error) {
		fdrop(fp);
		return (error);
	}

	/*
	 * Set fd for the fp file.
	 */
	fsetfd(fdp_to, fp, newfd);
	fdp_to->fd_files[newfd].fileflags = flags;
	fdrop(fp);

	return (newfd);
}

static struct sysvipc_req*
sysvipc_find_req(struct _req_list *list,
		struct lock *req_mtx, pid_t pid)
{
	struct sysvipc_req *reqtmp;
	lockmgr(req_mtx, LK_EXCLUSIVE);
	TAILQ_FOREACH(reqtmp, list, req_link) {
		if(reqtmp->proc->p_pid == pid) {
			lockmgr(req_mtx, LK_RELEASE);
			return reqtmp;
		}
	}
	lockmgr(req_mtx, LK_RELEASE);
	return NULL;
}

static int
get_proc_shm_cred(struct proc *p, struct ipc_perm *perm)
{
	struct ucred *cred;

	cred = p->p_ucred;
	perm->cuid = perm->uid = cred->cr_uid;
	perm->cgid = perm->gid = cred->cr_gid;

	return 1;
}

static int
sysvipc_dev_ioctl(struct dev_ioctl_args *ap)
{
	int error;
	struct client *cl;
	struct proc *proc_to, *proc_from;
	struct sysvipc_req *req;
	cdev_t dev = ap->a_head.a_dev;
	struct sysvipc_softc *sysv = dev->si_drv1;
	struct client_shm_data *client_data;

	error = 0;

	switch(ap->a_cmd) {
	case REGISTER_DAEMON:
		kprintf("[driver] register daemon\n");
		sysvipc_register(dev);
		break;
	case UNREGISTER_DAEMON:
		sysvipc_unregister(dev);
		break;
	case INSTALL_PIPE:
		kprintf("[driver] install pipe\n");

		if(curthread != sysv->sysvipc_daemon_thread)
			return (EPERM);

		cl = (struct client *)ap->a_data;

		//kprintf("sysv ipc pid = %d fd 0 = %d fd 1 = %d!\n",
		//cl->pid, cl->fd[0], cl->fd[1]);

		if(cl->pid <= 0)
			return (EINVAL);

		proc_from = curthread->td_proc;
		proc_to = pfind(cl->pid);

		/* Find the request associated with the client. */
		req = sysvipc_find_req(&sysv->consumed_list,
				&sysv->consumed_mtx, cl->pid);
		if(!req)
			return (EINVAL);

		/* Install for the client two file descriptors. 
		 * The first one is used for read and the second one for
		 * write purpose.
		 */
		req->fd[0] = sysvipc_install_fd(proc_from,
				proc_to, cl->fd[0]);
		req->fd[1] = sysvipc_install_fd(proc_from,
				proc_to, cl->fd[1]);
		PRELE(proc_to);

		/* Wakeup the client. */
		wakeup(req);

		break;
	case REGISTER_TO_DAEMON:

		if(sysv == NULL)
			return (EEXIST);

		if(curthread == sysv->sysvipc_daemon_thread)
			return (EPERM);

		kprintf("[driver] register to daemon\n");
		cl = (struct client *)ap->a_data;

		/* Allocate a struture for the request. */
		req = (struct sysvipc_req*)kmalloc( sizeof(*req),
				M_TEMP, M_ZERO | M_WAITOK);

		req->proc = curthread->td_proc;

		/* Add the request to the list used read be daemon. */
		lockmgr(&sysv->req_mtx, LK_EXCLUSIVE);
		TAILQ_INSERT_HEAD(&sysv->req_list, req, req_link);
		lockmgr(&sysv->req_mtx, LK_RELEASE);

		/* Wake up daemon to process the request. */
		wakeup(sysv);
		KNOTE(&sysv->sysvipc_rkq.ki_note, 0);

		/* Wait so that the daemon processes the request and
		 * installs the two new file descriptors.
		 */
		tsleep(req, 0, "regist", 0);

		//kprintf("client wake up %d %d\n", req->fd[0], req->fd[1]);
		cl->fd[0] = req->fd[0];
		cl->fd[1] = req->fd[1];

		/* Remove the request from the list of consumed requests. */
		lockmgr(&sysv->consumed_mtx, LK_EXCLUSIVE);
		TAILQ_REMOVE(&sysv->consumed_list, req, req_link);
		lockmgr(&sysv->consumed_mtx, LK_RELEASE);

		kfree(req, M_TEMP);
		break;
	case CONSUME_REQUEST:
		if(curthread != sysv->sysvipc_daemon_thread)
			return (EPERM);

		/* Wait for new requests. */
		lockmgr(&sysv->req_mtx, LK_EXCLUSIVE);
		while(TAILQ_EMPTY(&sysv->req_list))
			lksleep(sysv, &sysv->req_mtx, 0, "sysv", 0);
		kprintf("wake up to consume request\n");

		/* Remove the request from the req_list and add it to the
		 * list of consumed requests.
		 */
		req = TAILQ_LAST( &sysv->req_list, _req_list);
		if(!req)
			return (EINVAL);
		TAILQ_REMOVE(&sysv->req_list, req, req_link);
		lockmgr(&sysv->req_mtx, LK_RELEASE);

		lockmgr(&sysv->consumed_mtx, LK_EXCLUSIVE);
		TAILQ_INSERT_HEAD(&sysv->consumed_list, req, req_link);
		kprintf("pid received = %d\n", req->proc->p_pid);
		*(pid_t *)ap->a_data = req->proc->p_pid;
		lockmgr(&sysv->consumed_mtx, LK_RELEASE);

		break;
	case INSTALL_FD:
		if(curthread != sysv->sysvipc_daemon_thread)
			return (EPERM);

		kprintf("[driver] install fd\n");
		client_data = (struct client_shm_data *)ap->a_data;

		/* Get process structure. */
		proc_from = curthread->td_proc;
		proc_to = pfind(client_data->pid);

		/* Get client's credentials. */
		//get_proc_shm_cred(proc_to, &client_data->shm_perm);

		/* Install for the client the file descriptor. */
		client_data->fd = sysvipc_install_fd(proc_from,
				proc_to, client_data->fd);

		PRELE(proc_to);

		break;
	default:
		break;
	}

	return(error);
}


static int
sysvipc_modevent(module_t mod, int type, void *unused)
{
	struct sysvipc_softc *sysv;

	switch (type) {
	case MOD_LOAD:
		sysvipc_dev = make_dev(&sysvipc_dev_ops,
		    0,
		    UID_ROOT,
		    GID_WHEEL,
		    0600,
		    "sysvipc");

		kprintf("sysv ipc driver ready!\n");
		return 0;

	case MOD_UNLOAD:
		sysv = sysvipc_dev->si_drv1;
		if(sysv != NULL)
			sysvipc_unregister(sysvipc_dev);
		destroy_dev(sysvipc_dev);
		kprintf("sysv ipc driver unloaded.\n");
		return 0;
	}

	return EINVAL;
}

static moduledata_t sysvipc_mod = {
	"tessysvipc",
	sysvipc_modevent,
	0
};

DECLARE_MODULE(sysvipc, sysvipc_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(sysvipc, 1);
