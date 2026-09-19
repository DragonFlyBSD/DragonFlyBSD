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
#include <sys/vnode.h>
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
#define FW_FROMFILE	0x200	/* fw.data is ours to free */

	/*
	 * 'file' is private info managed by the autoload/unload code.
	 * Set at the end of firmware_get(), cleared only in the
	 * firmware_unload_task, so the latter can depend on its value even
	 * while the lock is not held.
	 */
	linker_file_t   file;	/* module file, if autoloaded */

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

/*
 * Where to look for firmware files, in order, before falling back to loading
 * a module of the same name.  /usr/local comes first because that is where a
 * package installs, and a package is the thing a user updates; the base entry
 * is there for images the system may one day ship itself.
 *
 * The image name is used as a relative path, so a driver asking for
 * "amdgpu/raven_vcn.bin" reads exactly the file linux-firmware ships under
 * that name, and a driver asking for "radeonkmsfw_BONAIRE_uvd" finds no file
 * and loads its module as before.
 */
static char firmware_path[MAXPATHLEN] =
    "/usr/local/lib/firmware;/lib/firmware";
SYSCTL_STRING(_hw, OID_AUTO, firmware_path, CTLFLAG_RW, firmware_path,
	      sizeof(firmware_path), "firmware image search path");
TUNABLE_STR("hw.firmware_path", firmware_path, sizeof(firmware_path));

/* Refuse to read anything absurd into wired memory. */
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
	struct priv_fw *match, *frp;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	/*
	 * Do a lookup to make sure the name is unique or find a free slot.
	 */
	match = lookup(imagename, &frp);
	if (match != NULL) {
		lockmgr(&firmware_lock, LK_RELEASE);
		kprintf("%s: image %s already registered!\n",
			__func__, imagename);
		return NULL;
	}
	if (frp == NULL) {
		lockmgr(&firmware_lock, LK_RELEASE);
		kprintf("%s: cannot register image %s, firmware table full!\n",
		    __func__, imagename);
		return NULL;
	}
	bzero(frp, sizeof(*frp));	/* start from a clean record */
	frp->fw.name = imagename;
	frp->fw.data = data;
	frp->fw.datasize = datasize;
	frp->fw.version = version;
	if (parent != NULL) {
		frp->parent = PRIV_FW(parent);
		frp->parent->refcnt++;
	}
	lockmgr(&firmware_lock, LK_RELEASE);
	if (bootverbose)
		kprintf("firmware: '%s' version %u: %zu bytes loaded at %p\n",
		    imagename, version, datasize, data);
	return &frp->fw;
}

/*
 * Unregister/remove a firmware image.  If there are outstanding
 * references an error is returned and the image is not removed
 * from the registry.
 */
int
firmware_unregister(const char *imagename)
{
	struct priv_fw *fp;
	int err;

	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	fp = lookup(imagename, NULL);
	if (fp == NULL) {
		/*
		 * It is ok for the lookup to fail; this can happen
		 * when a module is unloaded on last reference and the
		 * module unload handler unregister's each of it's
		 * firmware images.
		 */
		err = 0;
	} else if (fp->refcnt != 0) {	/* cannot unregister */
		err = EBUSY;
	}  else {
		linker_file_t x = fp->file;	/* save value */

		if (fp->parent != NULL)	/* release parent reference */
			fp->parent->refcnt--;
		if (fp->flags & FW_FROMFILE) {
			/*
			 * No module owns this image, so the registry does.
			 * Free it here, while the entry still points at it.
			 */
			kfree(__DECONST(void *, fp->fw.data), M_TEMP);
		}
		/*
		 * Clear the whole entry with bzero to make sure we
		 * do not forget anything. Then restore 'file' which is
		 * non-null for autoloaded images.
		 */
		bzero(fp, sizeof(struct priv_fw));
		fp->file = x;
		err = 0;
	}
	lockmgr(&firmware_lock, LK_RELEASE);
	return err;
}

/*
 * Register an image the loader preloaded under this name, if there is one.
 *
 * The loader records the path it resolved, so an entry asked for as
 * "amdgpu/polaris10_pfp.bin" is recorded as "/boot/firmware/amdgpu/
 * polaris10_pfp.bin".  Match the whole string, or the tail of it at a path
 * boundary, which is the same rule preload_search_by_name() applies one
 * component in.
 *
 * Called with firmware_lock held.  firmware_register() cannot be used from
 * here because it takes that lock itself, so the slot is filled in place;
 * the caller has just failed a lookup, and this one repeats it only to find
 * the free slot beside it.
 *
 * The image belongs to the preload area and is neither ours to free nor
 * backed by a module, so the entry gets no file and no FW_FROMFILE, and
 * unloadentry() leaves it alone.
 */
static int
loadpreloaded(const char *imagename)
{
	struct priv_fw *match, *frp;
	caddr_t mod, info;
	const char *recorded;
	void *data;
	size_t datasize, namelen;

	namelen = strlen(imagename);

	for (mod = preload_search_next_name(NULL); mod != NULL;
	     mod = preload_search_next_name(mod)) {
		size_t reclen;

		info = preload_search_info(mod, MODINFO_TYPE);
		if (info == NULL || strcmp(info, "firmware") != 0)
			continue;

		recorded = preload_search_info(mod, MODINFO_NAME);
		if (recorded == NULL)
			continue;
		reclen = strlen(recorded);
		if (reclen != namelen) {
			if (reclen < namelen + 1 ||
			    recorded[reclen - namelen - 1] != '/')
				continue;
		}
		if (strcmp(recorded + reclen - namelen, imagename) != 0)
			continue;

		info = preload_search_info(mod, MODINFO_ADDR);
		if (info == NULL)
			continue;
		data = *(void **)info;
		info = preload_search_info(mod, MODINFO_SIZE);
		if (info == NULL)
			continue;
		datasize = *(size_t *)info;
		if (data == NULL || datasize == 0)
			continue;

		match = lookup(imagename, &frp);
		if (match != NULL || frp == NULL)
			return (EEXIST);

		bzero(frp, sizeof(*frp));
		frp->fw.name = imagename;
		frp->fw.data = data;
		frp->fw.datasize = datasize;
		frp->fw.version = 0;
		if (bootverbose) {
			kprintf("firmware: '%s' preloaded, %zu bytes at %p\n",
				imagename, datasize, data);
		}
		return (0);
	}

	return (ENOENT);
}

/*
 * Read one firmware image out of the filesystem and register it.
 * Returns 0 if the image is now in the registry.
 *
 * Runs from the firmware taskqueue, which is where the module path already
 * runs precisely because it needs a directory context to do i/o.
 */
static int
loadfile(const char *imagename)
{
	struct nlookupdata nd;
	struct vnode *vp = NULL;
	struct vattr vattr;
	struct priv_fw *match, *frp;
	char *path, *data = NULL;
	const char *cp, *ep;
	size_t datasize = 0;
	int error, resid;
	int found = 0;

	path = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);

	for (cp = firmware_path; *cp != '\0'; cp = (*ep == '\0') ? ep : ep + 1) {
		for (ep = cp; *ep != '\0' && *ep != ';'; ep++)
			continue;
		if (ep == cp)
			continue;

		ksnprintf(path, MAXPATHLEN, "%.*s/%s", (int)(ep - cp), cp,
			  imagename);

		error = nlookup_init(&nd, path, UIO_SYSSPACE,
				     NLC_FOLLOW | NLC_LOCKVP);
		if (error == 0)
			error = vn_open(&nd, NULL, FREAD, 0);
		if (error != 0) {
			nlookup_done(&nd);
			continue;
		}
		vp = nd.nl_open_vp;
		nd.nl_open_vp = NULL;
		nlookup_done(&nd);

		if (vp->v_type != VREG)
			goto next;
		if (VOP_GETATTR(vp, &vattr) != 0)
			goto next;
		if (vattr.va_size == 0 || vattr.va_size > firmware_max_size) {
			kprintf("firmware: %s is %ju bytes, refusing\n",
				path, (uintmax_t)vattr.va_size);
			goto next;
		}

		datasize = vattr.va_size;
		data = kmalloc(datasize, M_TEMP, M_WAITOK);
		error = vn_rdwr(UIO_READ, vp, data, datasize, 0, UIO_SYSSPACE,
				IO_NODELOCKED, proc0.p_ucred, &resid);
		if (error != 0 || resid != 0) {
			kprintf("firmware: %s: read failed (%d)\n", path,
				error);
			kfree(data, M_TEMP);
			data = NULL;
			goto next;
		}
		found = 1;
next:
		vn_unlock(vp);
		vn_close(vp, FREAD, NULL);
		vp = NULL;
		if (found)
			break;
	}

	kfree(path, M_TEMP);
	if (!found)
		return (ENOENT);

	/*
	 * Fill the slot under one lock rather than calling
	 * firmware_register() and setting FW_FROMFILE afterwards.  Between
	 * those two points the entry would describe memory as borrowed that
	 * is in fact ours, and a firmware_put() landing in the window would
	 * mark it for unload without the flag that says how to release it.
	 */
	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	match = lookup(imagename, &frp);
	if (match != NULL || frp == NULL) {
		lockmgr(&firmware_lock, LK_RELEASE);
		kprintf("%s: cannot register image %s, %s\n", __func__,
			imagename,
			match != NULL ? "already registered" : "table full");
		kfree(data, M_TEMP);
		return (match != NULL ? EEXIST : ENOSPC);
	}
	bzero(frp, sizeof(*frp));
	frp->fw.name = imagename;
	frp->fw.data = data;
	frp->fw.datasize = datasize;
	frp->fw.version = 0;
	frp->flags = FW_FROMFILE;
	lockmgr(&firmware_lock, LK_RELEASE);

	if (bootverbose) {
		kprintf("firmware: '%s' from a file, %zu bytes at %p\n",
			imagename, datasize, data);
	}

	return (0);
}

static void
loadimage(void *arg, int npending)
{
#ifdef notyet
	struct thread *td = curthread;
#endif
	char *imagename = arg;
	struct priv_fw *fp;
	linker_file_t result;
	int error;

	/* synchronize with the thread that dispatched us */
	lockmgr(&firmware_lock, LK_EXCLUSIVE);
	lockmgr(&firmware_lock, LK_RELEASE);

/* JAT
	if (td->td_proc->p_fd->fd_rdir == NULL) {
		kprintf("%s: root not mounted yet, no way to load image\n",
		    imagename);
		goto done;
	}
*/
	/*
	 * A file, if there is one, wins over a module of the same name: it is
	 * the copy the administrator installed and can update.
	 */
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
		(void) linker_release_module(imagename, NULL, NULL);
		goto done;
	}
	fp->file = result;	/* record the module identity */
	lockmgr(&firmware_lock, LK_RELEASE);
done:
	wakeup_one(imagename);		/* we're done */
}

/*
 * Lookup and potentially load the specified firmware image.
 * If the firmware is not found in the registry, try to load a kernel
 * module named as the image name.
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
	 * An image the loader preloaded is already in memory.  It needs
	 * neither a filesystem nor a privilege nor the taskqueue below, and
	 * it is the only source that works before the root filesystem is
	 * mounted - the deferred path does nothing at all while cold.
	 */
	if (loadpreloaded(imagename) == 0) {
		fp = lookup(imagename, NULL);
		if (fp != NULL)
			goto found;
	}
	/*
	 * Image not present, try to load the module holding it.
	 */
	if (caps_priv_check_self(SYSCAP_NOKLD) != 0 || securelevel > 0) {
		lockmgr(&firmware_lock, LK_RELEASE);
		kprintf("%s: insufficient privileges to "
		    "load firmware image %s\n", __func__, imagename);
		return NULL;
	}
	/*
	 * Defer load to a thread with known context.  linker_reference_module
	 * may do filesystem i/o which requires root & current dirs, etc.
	 * Also we must not hold any lock's over this call which is problematic.
	 */
	if (!cold) {
		TASK_INIT(&fwload_task, 0, loadimage, __DECONST(void *,
		    imagename));
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

#ifdef notyet
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
#endif

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

		if (fp->flags & FW_FROMFILE) {
			/* Ours, and nothing to unload: just let it go. */
			const char *name = fp->fw.name;

			fp->flags &= ~FW_UNLOAD;
			lockmgr(&firmware_lock, LK_RELEASE);
			firmware_unregister(name);
			lockmgr(&firmware_lock, LK_EXCLUSIVE);
			limit = i + FIRMWARE_MAX;
			continue;
		}
		if (fp->file == NULL)
			continue;

		/*
		 * Found an entry. Now:
		 * 1. bump up limit to make sure we make another full round;
		 * 2. clear FW_UNLOAD so we don't try this entry again.
		 * 3. release the lock while trying to unload the module.
		 * 'file' remains set so that the entry cannot be reused
		 * in the meantime (it also means that fp->file will
		 * not change while we release the lock).
		 */
		limit = i + FIRMWARE_MAX;	/* make another full round */
		fp->flags &= ~FW_UNLOAD;	/* do not try again */

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
		(void) taskqueue_start_threads(&firmware_tq, 1, TDPRI_KERN_DAEMON,
		    -1, "firmware taskq");
		if (rootvnode != NULL) {
			/*
			 * Root is already mounted so we won't get an event;
			 * simulate one here.
			 */
#ifdef notyet
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
				kprintf("%s: image %p ref %d still active slot %d\n",
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
