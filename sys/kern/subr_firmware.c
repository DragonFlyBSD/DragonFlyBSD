/*-
 * Copyright (c) 2005-2008, Sam Leffler <sam@errno.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/subr_firmware.c,v 1.13.2.2 2010/02/11 18:34:06 mjacob Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/errno.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/caps.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/module.h>
#include <sys/eventhandler.h>

#include <sys/filedesc.h>
#include <sys/vnode.h>

/*
 * Loadable firmware support. See sys/sys/firmware.h and firmware(9)
 * form more details on the subsystem.
 *
 * 'struct firmware' is the user-visible part of the firmware table.
 * Additional internal information is stored in a 'struct priv_fw'
 * (currently a static array). A slot is in use if FW_INUSE is true:
 */

#define FW_INUSE(p)	((p)->file != NULL || (p)->fw.name != NULL)

/*
 * fw.name != NULL when an image is registered; file != NULL for
 * autoloaded images whose handling has not been completed.
 *
 * The state of a slot evolves as follows:
 *	firmware_register	-->  fw.name = image_name
 *	(autoloaded image)	-->  file = module reference
 *	firmware_unregister	-->  fw.name = NULL
 *	(unloadentry complete)	-->  file = NULL
 *
 * In order for the above to work, the 'file' field must remain
 * unchanged in firmware_unregister().
 *
 * Images residing in the same module are linked to each other
 * through the 'parent' argument of firmware_register().
 * One image (typically, one with the same name as the module to let
 * the autoloading mechanism work) is considered the parent image for
 * all other images in the same module. Children affect the refcount
 * on the parent image preventing improper unloading of the image itself.
 */

struct priv_fw {
	int		refcnt;		/* reference count */

	/*
	 * parent entry, see above. Set on firmware_register(),
	 * cleared on firmware_unregister().
	 */
	struct priv_fw	*parent;

	int 		flags;	/* record FIRMWARE_UNLOAD requests */
#define FW_UNLOAD	0x100
#define FW_FROMFILE	0x200	/* fw.name and fw.data are ours to free */

	/*
	 * 'file' is private info managed by the autoload/unload code.
	 * Set at the end of firmware_get(), cleared only in the
	 * firmware_unload_task, so the latter can depend on its value even
	 * while the lock is not held.
	 */
	linker_file_t	file;	/* module file, if autoloaded */

	/*
	 * 'fw' is the externally visible image information.
	 * We do not make it the first field in priv_fw, to avoid the
	 * temptation of casting pointers to each other.
	 * Use PRIV_FW(fw) to get a pointer to the cointainer of fw.
	 * Beware, PRIV_FW does not work for a NULL pointer.
	 */
	struct firmware	fw;	/* externally visible information */
};

/*
 * PRIV_FW returns the pointer to the container of struct firmware *x.
 * Cast to intptr_t to override the 'const' attribute of x
 */
#define PRIV_FW(x)	((struct priv_fw *)		\
	((intptr_t)(x) - offsetof(struct priv_fw, fw)) )

/*
 * At the moment we use a static array as backing store for the registry.
 * Should we move to a dynamic structure, keep in mind that we cannot
 * reallocate the array because pointers are held externally.
 * A list may work, though.
 */
#define	FIRMWARE_MAX	128
static struct priv_fw firmware_table[FIRMWARE_MAX];

/*
 * Firmware module operations are handled in a separate task as they
 * might sleep and they require directory context to do i/o.
 */
static struct taskqueue *firmware_tq;
static struct task firmware_unload_task;

/*
 * This lock protects accesses to the firmware table.
 */
static struct lock firmware_lock;

static MALLOC_DEFINE(M_FIRMWARE, "firmware", "Firmware images and names");

/*
 * Where to look for firmware files, in order, before falling back to loading
 * a module of the same name.
 */
static char firmware_path[MAXPATHLEN] =
	"/usr/local/lib/firmware;/usr/lib/firmware";
SYSCTL_STRING(_hw, OID_AUTO, firmware_path, CTLFLAG_RW, firmware_path,
	      sizeof(firmware_path), "firmware image search path");
TUNABLE_STR("hw.firmware_path", firmware_path, sizeof(firmware_path));

static u_long firmware_max_size = 256 * 1024 * 1024;
SYSCTL_ULONG(_hw, OID_AUTO, firmware_max_size, CTLFLAG_RW,
	     &firmware_max_size, 0, "largest firmware image read from a file");

/*
 * Helper function to lookup a name.
 * As a side effect, it sets the pointer to a free slot, if any.
 * This way we can concentrate most of the registry scanning in
 * this function, which makes it easier to replace the registry
 * with some other data structure.
 */
static struct priv_fw *
lookup(const char *name, struct priv_fw **empty_slot)
{
	struct priv_fw *fp = NULL;
	struct priv_fw *dummy;
	int i;

	if (empty_slot == NULL)
		empty_slot = &dummy;
	*empty_slot = NULL;
	for (i = 0; i < FIRMWARE_MAX; i++) {
		fp = &firmware_table[i];
		if (fp->fw.name != NULL && strcasecmp(name, fp->fw.name) == 0)
			break;
		else if (!FW_INUSE(fp))
			*empty_slot = fp;
	}
	return (i < FIRMWARE_MAX ) ? fp : NULL;
}

static int
firmware_register_locked(const char *imagename, const void *data,
    size_t datasize, unsigned int version, const struct firmware *parent,
    int flags, struct priv_fw **result)
{
	struct priv_fw *match, *frp;

	match = lookup(imagename, &frp);
	if (match != NULL)
		return (EEXIST);
	if (frp == NULL)
		return (ENOSPC);

	bzero(frp, sizeof(*frp));
	frp->fw.name = imagename;
	frp->fw.data = data;
	frp->fw.datasize = datasize;
	frp->fw.version = version;
	frp->flags = flags;
	if (parent != NULL) {
		frp->parent = PRIV_FW(parent);
		frp->parent->refcnt++;
	}

	if (result != NULL)
		*result = frp;
	return (0);
}

/*
 * Register a firmware image with the specified name.  The
 * image name must not already be registered.  If this is a
 * subimage then parent refers to a previously registered
 * image that this should be associated with.
 */
const struct firmware *
firmware_register(const char *imagename, const void *data, size_t datasize,
    unsigned int version, const struct firmware *parent)
{
	struct priv_fw *fp;
	int error;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	error = firmware_register_locked(imagename, data, datasize, version,
	    parent, 0, &fp);
	lockmgr(&firmware_lock, LK_RELEASE);
	if (error != 0) {
		kprintf("firmware: cannot register image %s, error=%d\n",
		    imagename, error);
		return (NULL);
	}
	if (bootverbose) {
		kprintf("firmware: '%s' version %u: %zu bytes loaded at %p\n",
		    imagename, version, datasize, data);
	}
	return (&fp->fw);
}

static int
firmware_unregister_locked(struct priv_fw *fp)
{
	linker_file_t file;

	if (fp == NULL) {
		/*
		 * It is ok for the lookup to fail; this can happen
		 * when a module is unloaded on last reference and the
		 * module unload handler unregister's each of it's
		 * firmware images.
		 */
		return (0);
	}
	if (fp->refcnt != 0)
		return (EBUSY);

	file = fp->file;	/* save value */
	if (fp->parent != NULL)
		fp->parent->refcnt--;
	if (fp->flags & FW_FROMFILE) {
		kfree(__DECONST(void *, fp->fw.data), M_FIRMWARE);
		kfree(__DECONST(char *, fp->fw.name), M_FIRMWARE);
	}
	/*
	 * Clear the whole entry with bzero to make sure we
	 * do not forget anything. Then restore 'file' which is
	 * non-null for autoloaded images.
	 */
	bzero(fp, sizeof(*fp));
	fp->file = file;
	return (0);
}

/*
 * Unregister/remove a firmware image.  If there are outstanding
 * references an error is returned and the image is not removed
 * from the registry.
 */
int
firmware_unregister(const char *imagename)
{
	int error;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	error = firmware_unregister_locked(lookup(imagename, NULL));
	lockmgr(&firmware_lock, LK_RELEASE);
	return (error);
}

/*
 * Register an image the loader preloaded under this name, if there is one.
 */
static struct priv_fw *
loadpreloaded(const char *imagename)
{
	struct priv_fw *fp;
	caddr_t mod, info;
	const char *fwname;
	void *data;
	size_t datasize;
	int error;

	KKASSERT(lockstatus(&firmware_lock, curthread) == LK_EXCLUSIVE);

	mod = preload_search_by_name(imagename);
	if (mod == NULL)
		return (NULL);

	/* Get a stable name pointer for firmware_register_locked(). */
	info = preload_search_info(mod, MODINFO_NAME);
	fwname = (const char *)info + strlen(info) - strlen(imagename);
	KKASSERT(strcmp(fwname, imagename) == 0);

	/* It's unlikely to have duplicate names, but still do a check. */
	info = preload_search_info(mod, MODINFO_TYPE);
	if (info == NULL || strcmp(info, "firmware") != 0) {
		kprintf("firmware: preloaded image %s has wrong type %s\n",
			imagename, (char *)info);
		return (NULL);
	}

	info = preload_search_info(mod, MODINFO_ADDR);
	if (info == NULL) {
		kprintf("firmware: preloaded image %s does not have data\n",
			imagename);
		return (NULL);
	}
	data = *(void **)info;
	info = preload_search_info(mod, MODINFO_SIZE);
	if (info == NULL) {
		kprintf("firmware: preloaded image %s does not have a size\n",
			imagename);
		return (NULL);
	}
	datasize = *(size_t *)info;
	if (data == NULL || datasize == 0) {
		kprintf("firmware: preloaded image %s is empty\n",
			imagename);
		return (NULL);
	}

	error = firmware_register_locked(fwname, data, datasize,
	    0 /* version */, NULL /* parent */, 0 /* flags */, &fp);
	if (error != 0) {
		kprintf("firmware: cannot register preloaded image %s, "
			"error=%d\n",
			imagename, error);
		return (NULL);
	}

	if (bootverbose) {
		kprintf("firmware: registered preloaded image %s, "
			"%zu bytes at %p\n",
			imagename, datasize, data);
	}
	return (fp);
}

/*
 * Read one firmware image out of the filesystem and register it.
 * Returns 0 if the image is now in the registry.
 *
 * Runs from the firmware taskqueue because it needs a directory context to do
 * I/O.
 */
static int
loadfile(const char *imagename)
{
	struct nlookupdata nd;
	struct vnode *vp;
	struct vattr vattr;
	char *path, *name, *data;
	const char *cp, *ep;
	size_t datasize;
	int error, resid;

	vp = NULL;
	data = NULL;
	path = kmalloc(MAXPATHLEN, M_FIRMWARE, M_WAITOK);

	for (cp = firmware_path; *cp != '\0'; cp = (*ep == '\0' ? ep : ep+1)) {
		for (ep = cp; *ep != '\0' && *ep != ';'; ep++)
			;
		if (ep == cp)
			continue;

		ksnprintf(path, MAXPATHLEN, "%.*s/%s",
			  (int)(ep - cp), cp, imagename);
		error = nlookup_init(&nd, path, UIO_SYSSPACE,
				     NLC_FOLLOW | NLC_LOCKVP);
		if (error == 0)
			error = vn_open(&nd, NULL, FREAD, 0);
		if (error == 0 && nd.nl_open_vp->v_type == VREG) {
			vp = nd.nl_open_vp;
			nd.nl_open_vp = NULL;
			nlookup_done(&nd);
			if (bootverbose) {
				kprintf("firmware: found '%s' from file %s\n",
					imagename, path);
			}
			break;
		}
		nlookup_done(&nd);
	}

	if (vp == NULL) {
		error = ENOENT;
		goto error;
	}

	if (VOP_GETATTR(vp, &vattr) != 0) {
		error = EINVAL;
		goto error;
	}
	if (vattr.va_size <= 0 || (u_long)vattr.va_size > firmware_max_size) {
		kprintf("firmware: file %s is %ju bytes, refusing\n",
			path, (uintmax_t)vattr.va_size);
		error = EINVAL;
		goto error;
	}

	datasize = (size_t)vattr.va_size;
	data = kmalloc(datasize, M_FIRMWARE, M_WAITOK | M_NULLOK);
	if (data == NULL) {
		error = ENOMEM;
		goto error;
	}
	/* The taskqueue has no process, so use proc0. */
	error = vn_rdwr(UIO_READ, vp, data, datasize, 0, UIO_SYSSPACE,
			IO_NODELOCKED, proc0.p_ucred, &resid);
	if (error != 0 || resid != 0) {
		if (error == 0)
			error = EIO;
		kprintf("firmware: file %s: read failed: error=%d, resid=%d\n",
			path, error, resid);
		goto error;
	}

	vn_unlock(vp);
	vn_close(vp, FREAD, NULL);
	vp = NULL;

	strlcpy(path, imagename, MAXPATHLEN);
	name = path;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	error = firmware_register_locked(name, data, datasize, 0 /* version */,
	    NULL /* parent */, FW_FROMFILE /* flags */, NULL /* result */);
	lockmgr(&firmware_lock, LK_RELEASE);
	if (error != 0) {
		kprintf("firmware: cannot register firmware %s, error=%d\n",
			imagename, error);
		goto error;
	}

	if (bootverbose) {
		kprintf("firmware: loaded '%s', %zu bytes at %p\n",
			imagename, datasize, data);
	}
	return (0);

error:
	if (vp != NULL) {
		vn_unlock(vp);
		vn_close(vp, FREAD, NULL);
	}
	if (data != NULL)
		kfree(data, M_FIRMWARE);
	kfree(path, M_FIRMWARE);
	return (error);
}

static void
loadimage(void *arg, int npending)
{
#if 0 /* not yet */
	struct thread *td = curthread;
#endif
	const char *imagename = arg;
	struct priv_fw *fp;
	linker_file_t result;
	int error;

	/* synchronize with the thread that dispatched us */
	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	lockmgr(&firmware_lock, LK_RELEASE);

#if 0 /* not yet (JAT) */
	if (td->td_proc->p_fd->fd_rdir == NULL) {
		kprintf("%s: root not mounted yet, no way to load image\n",
		    imagename);
		goto done;
	}
#endif

	/* Prefer a firmware file over a module wrapping the firmware. */
	if (loadfile(imagename) == 0)
		goto done;

	error = linker_reference_module(imagename, NULL, &result);
	if (error != 0) {
		kprintf("%s: could not load firmware image, error %d\n",
		    imagename, error);
		goto done;
	}

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	fp = lookup(imagename, NULL);
	if (fp == NULL || fp->file != NULL) {
		lockmgr(&firmware_lock, LK_RELEASE);
		if (fp == NULL)
			kprintf("%s: firmware image loaded, "
			    "but did not register\n", imagename);
		linker_release_module(imagename, NULL, NULL);
		goto done;
	}
	fp->file = result;	/* record the module identity */
	lockmgr(&firmware_lock, LK_RELEASE);
done:
	wakeup_one(imagename);		/* we're done */
}

/*
 * Lookup and potentially load the specified firmware image.
 *
 * If the firmware is located, a reference is returned. The caller must
 * release this reference for the image to be eligible for removal/unload.
 */
const struct firmware *
firmware_get(const char *imagename)
{
	struct task fwload_task;
	struct priv_fw *fp;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	fp = lookup(imagename, NULL);
	if (fp != NULL)
		goto found;
	/*
	 * Check for an in-memory image preloaded by the loader.
	 */
	fp = loadpreloaded(imagename);
	if (fp != NULL)
		goto found;
	/*
	 * Image not present, try to load it with loadimage().
	 */
	if (caps_priv_check_self(SYSCAP_NOKLD) != 0 || securelevel > 0) {
		lockmgr(&firmware_lock, LK_RELEASE);
		kprintf("%s: insufficient privileges to "
		    "load firmware image %s\n", __func__, imagename);
		return NULL;
	}
	/*
	 * Defer load to a thread with known context.  loadimage()
	 * may do filesystem i/o which requires root & current dirs, etc.
	 * Also we must not hold any lock's over this call which is problematic.
	 */
	if (!cold) {
		TASK_INIT(&fwload_task, 0, loadimage,
		    __DECONST(void *, imagename));
		taskqueue_enqueue(firmware_tq, &fwload_task);
		lksleep(__DECONST(void *, imagename), &firmware_lock, 0,
		    "fwload", 0);
	}
	/*
	 * After attempting to load the module, see if the image is registered.
	 */
	fp = lookup(imagename, NULL);
	if (fp == NULL) {
		lockmgr(&firmware_lock, LK_RELEASE);
		return NULL;
	}
found:				/* common exit point on success */
	fp->refcnt++;
	lockmgr(&firmware_lock, LK_RELEASE);
	return &fp->fw;
}

/*
 * Release a reference to a firmware image returned by firmware_get.
 * The caller may specify, with the FIRMWARE_UNLOAD flag, its desire
 * to release the resource, but the flag is only advisory.
 *
 * If this is the last reference to the firmware image, and this is an
 * autoloaded module, wake up the firmware_unload_task to figure out
 * what to do with the associated module.
 */
void
firmware_put(const struct firmware *p, int flags)
{
	struct priv_fw *fp = PRIV_FW(p);

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	fp->refcnt--;
	if (fp->refcnt == 0) {
		if (flags & FIRMWARE_UNLOAD)
			fp->flags |= FW_UNLOAD;
		if (fp->file || (fp->flags & FW_FROMFILE))
			taskqueue_enqueue(firmware_tq, &firmware_unload_task);
	}
	lockmgr(&firmware_lock, LK_RELEASE);
}

#if 0 /* not yet */
/*
 * Setup directory state for the firmware_tq thread so we can do i/o.
 */
static void
set_rootvnode(void *arg, int npending)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;


#if 0
	spin_lock_wr(&p->p_fd->fd_spin);
	if (p->p_fd->fd_cdir == NULL) {
		p->p_fd->fd_cdir = rootvnode;
		vref(rootvnode);
	}
	if (p->p_fd->fd_rdir == NULL) {
		p->p_fd->fd_rdir = rootvnode;
		vref(rootvnode);
	}
	spin_unlock_wr(&p->p_fd->fd_spin);

	kfree(arg, M_TEMP);
#endif
}

/*
 * Event handler called on mounting of /; bounce a task
 * into the task queue thread to setup it's directories.
 */
static void
firmware_mountroot(void *arg)
{
	struct task *setroot_task;

	setroot_task = kmalloc(sizeof(struct task), M_TEMP, M_NOWAIT);
	if (setroot_task != NULL) {
		TASK_INIT(setroot_task, 0, set_rootvnode, setroot_task);
		taskqueue_enqueue(firmware_tq, setroot_task);
	} else
		kprintf("%s: no memory for task!\n", __func__);
}
EVENTHANDLER_DECLARE(mountroot, firmware_mountroot);
#endif /* not yet */

/*
 * The body of the task in charge of unloading autoloaded modules
 * that are not needed anymore.
 * Images can be cross-linked so we may need to make multiple passes,
 * but the time we spend in the loop is bounded because we clear entries
 * as we touch them.
 */
static void
unloadentry(void *unused1, int unused2)
{
	int limit = FIRMWARE_MAX;
	int i;	/* current cycle */

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	/*
	 * Scan the table. limit is set to make sure we make another
	 * full sweep after matching an entry that requires unloading.
	 */
	for (i = 0; i < limit; i++) {
		struct priv_fw *fp;
		int err;

		fp = &firmware_table[i % FIRMWARE_MAX];
		if (fp->fw.name == NULL || fp->refcnt != 0 ||
		    (fp->flags & FW_UNLOAD) == 0)
			continue;
		if (fp->file == NULL && (fp->flags & FW_FROMFILE) == 0)
			continue;

		limit = i + FIRMWARE_MAX;	/* make another full round */
		fp->flags &= ~FW_UNLOAD;	/* do not try again */

		if (fp->flags & FW_FROMFILE) {
			err = firmware_unregister_locked(fp);
			KKASSERT(err == 0);
			continue;
		}

		/* The module reference pins this slot while the lock is dropped. */
		lockmgr(&firmware_lock, LK_RELEASE);
		err = linker_release_module(NULL, NULL, fp->file);
		lockmgr(&firmware_lock, LK_EXCLUSIVE);

		/*
		 * We rely on the module to call firmware_unregister()
		 * on unload to actually release the entry.
		 * If err = 0 we can drop our reference as the system
		 * accepted it. Otherwise unloading failed (e.g. the
		 * module itself gave an error) so our reference is
		 * still valid.
		 */
		if (err == 0)
			fp->file = NULL;
	}
	lockmgr(&firmware_lock, LK_RELEASE);
}

/*
 * Module glue.
 */
static int
firmware_modevent(module_t mod, int type, void *unused)
{
	struct priv_fw *fp;
	int i, err;

	switch (type) {
	case MOD_LOAD:
		TASK_INIT(&firmware_unload_task, 0, unloadentry, NULL);
		lockinit(&firmware_lock, "firmware table", 0, LK_CANRECURSE);
		firmware_tq = taskqueue_create("taskqueue_firmware", M_WAITOK,
		    taskqueue_thread_enqueue, &firmware_tq);
		/* NB: use our own loop routine that sets up context */
		taskqueue_start_threads(&firmware_tq, 1, TDPRI_KERN_DAEMON,
		    -1, "firmware taskq");
		if (rootvnode != NULL) {
			/*
			 * Root is already mounted so we won't get an event;
			 * simulate one here.
			 */
#if 0 /* not yet */
			firmware_mountroot(NULL);
#endif
		}
		return 0;

	case MOD_UNLOAD:
		/* request all autoloaded modules to be released */
		lockmgr(&firmware_lock, LK_EXCLUSIVE);
		for (i = 0; i < FIRMWARE_MAX; i++) {
			fp = &firmware_table[i];
			fp->flags |= FW_UNLOAD;
		}
		lockmgr(&firmware_lock, LK_RELEASE);
		taskqueue_enqueue(firmware_tq, &firmware_unload_task);
		taskqueue_drain(firmware_tq, &firmware_unload_task);
		err = 0;
		for (i = 0; i < FIRMWARE_MAX; i++) {
			fp = &firmware_table[i];
			if (fp->fw.name != NULL) {
				kprintf("%s: image %p ref %d still active "
					"slot %d\n",
					__func__, fp->fw.name,
					fp->refcnt,  i);
				err = EINVAL;
			}
		}
		if (err == 0)
			taskqueue_free(firmware_tq);
		return err;
	}
	return EINVAL;
}

static moduledata_t firmware_mod = {
	"firmware",
	firmware_modevent,
	NULL
};
DECLARE_MODULE(firmware, firmware_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(firmware, 1);
