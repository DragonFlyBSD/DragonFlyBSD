/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/file.h>
#include <sys/msgport.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>
#include <sys/devfs.h>
#include <sys/devfs_rules.h>
#include <sys/udev.h>

#include <sys/msgport2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>
#include <sys/sysref2.h>

MALLOC_DEFINE(M_DEVFS, "devfs", "Device File System (devfs) allocations");
DEVFS_DECLARE_CLONE_BITMAP(ops_id);
/*
 * SYSREF Integration - reference counting, allocation,
 * sysid and syslink integration.
 */
static void devfs_cdev_terminate(cdev_t dev);
static void devfs_cdev_lock(cdev_t dev);
static void devfs_cdev_unlock(cdev_t dev);
static struct sysref_class     cdev_sysref_class = {
	.name =         "cdev",
	.mtype =        M_DEVFS,
	.proto =        SYSREF_PROTO_DEV,
	.offset =       offsetof(struct cdev, si_sysref),
	.objsize =      sizeof(struct cdev),
	.nom_cache =	32,
	.flags =        0,
	.ops =  {
		.terminate = (sysref_terminate_func_t)devfs_cdev_terminate,
		.lock = (sysref_lock_func_t)devfs_cdev_lock,
		.unlock = (sysref_unlock_func_t)devfs_cdev_unlock
	}
};

static struct objcache	*devfs_node_cache;
static struct objcache 	*devfs_msg_cache;
static struct objcache	*devfs_dev_cache;

static struct objcache_malloc_args devfs_node_malloc_args = {
	sizeof(struct devfs_node), M_DEVFS };
struct objcache_malloc_args devfs_msg_malloc_args = {
	sizeof(struct devfs_msg), M_DEVFS };
struct objcache_malloc_args devfs_dev_malloc_args = {
	sizeof(struct cdev), M_DEVFS };

static struct devfs_dev_head devfs_dev_list =
		TAILQ_HEAD_INITIALIZER(devfs_dev_list);
static struct devfs_mnt_head devfs_mnt_list =
		TAILQ_HEAD_INITIALIZER(devfs_mnt_list);
static struct devfs_chandler_head devfs_chandler_list =
		TAILQ_HEAD_INITIALIZER(devfs_chandler_list);
static struct devfs_alias_head devfs_alias_list =
		TAILQ_HEAD_INITIALIZER(devfs_alias_list);
static struct devfs_dev_ops_head devfs_dev_ops_list =
		TAILQ_HEAD_INITIALIZER(devfs_dev_ops_list);

struct lock 		devfs_lock;
static struct lwkt_port devfs_dispose_port;
static struct lwkt_port devfs_msg_port;
static struct thread 	*td_core;

static struct spinlock  ino_lock;
static ino_t 	d_ino;
static int	devfs_debug_enable;
static int	devfs_run;

static ino_t devfs_fetch_ino(void);
static int devfs_create_all_dev_worker(struct devfs_node *);
static int devfs_create_dev_worker(cdev_t, uid_t, gid_t, int);
static int devfs_destroy_dev_worker(cdev_t);
static int devfs_destroy_related_worker(cdev_t);
static int devfs_destroy_dev_by_ops_worker(struct dev_ops *, int);
static int devfs_propagate_dev(cdev_t, int);
static int devfs_unlink_dev(cdev_t dev);
static void devfs_msg_exec(devfs_msg_t msg);

static int devfs_chandler_add_worker(const char *, d_clone_t *);
static int devfs_chandler_del_worker(const char *);

static void devfs_msg_autofree_reply(lwkt_port_t, lwkt_msg_t);
static void devfs_msg_core(void *);

static int devfs_find_device_by_name_worker(devfs_msg_t);
static int devfs_find_device_by_udev_worker(devfs_msg_t);

static int devfs_apply_reset_rules_caller(char *, int);

static int devfs_scan_callback_worker(devfs_scan_t *, void *);

static struct devfs_node *devfs_resolve_or_create_dir(struct devfs_node *,
		char *, size_t, int);

static int devfs_make_alias_worker(struct devfs_alias *);
static int devfs_destroy_alias_worker(struct devfs_alias *);
static int devfs_alias_remove(cdev_t);
static int devfs_alias_reap(void);
static int devfs_alias_propagate(struct devfs_alias *, int);
static int devfs_alias_apply(struct devfs_node *, struct devfs_alias *);
static int devfs_alias_check_create(struct devfs_node *);

static int devfs_clr_related_flag_worker(cdev_t, uint32_t);
static int devfs_destroy_related_without_flag_worker(cdev_t, uint32_t);

static void *devfs_reaperp_callback(struct devfs_node *, void *);
static void *devfs_gc_dirs_callback(struct devfs_node *, void *);
static void *devfs_gc_links_callback(struct devfs_node *, struct devfs_node *);
static void *
devfs_inode_to_vnode_worker_callback(struct devfs_node *, ino_t *);

/*
 * devfs_debug() is a SYSCTL and TUNABLE controlled debug output function
 * using kvprintf
 */
int
devfs_debug(int level, char *fmt, ...)
{
	__va_list ap;

	__va_start(ap, fmt);
	if (level <= devfs_debug_enable)
		kvprintf(fmt, ap);
	__va_end(ap);

	return 0;
}

/*
 * devfs_allocp() Allocates a new devfs node with the specified
 * parameters. The node is also automatically linked into the topology
 * if a parent is specified. It also calls the rule and alias stuff to
 * be applied on the new node
 */
struct devfs_node *
devfs_allocp(devfs_nodetype devfsnodetype, char *name,
	     struct devfs_node *parent, struct mount *mp, cdev_t dev)
{
	struct devfs_node *node = NULL;
	size_t namlen = strlen(name);

	node = objcache_get(devfs_node_cache, M_WAITOK);
	bzero(node, sizeof(*node));

	atomic_add_long(&DEVFS_MNTDATA(mp)->leak_count, 1);

	node->d_dev = NULL;
	node->nchildren = 1;
	node->mp = mp;
	node->d_dir.d_ino = devfs_fetch_ino();

	/*
	 * Cookie jar for children. Leave 0 and 1 for '.' and '..' entries
	 * respectively.
	 */
	node->cookie_jar = 2;

	/*
	 * Access Control members
	 */
	node->mode = DEVFS_DEFAULT_MODE;
	node->uid = DEVFS_DEFAULT_UID;
	node->gid = DEVFS_DEFAULT_GID;

	switch (devfsnodetype) {
	case Nroot:
		/*
		 * Ensure that we don't recycle the root vnode by marking it as
		 * linked into the topology.
		 */
		node->flags |= DEVFS_NODE_LINKED;
	case Ndir:
		TAILQ_INIT(DEVFS_DENODE_HEAD(node));
		node->d_dir.d_type = DT_DIR;
		node->nchildren = 2;
		break;

	case Nlink:
		node->d_dir.d_type = DT_LNK;
		break;

	case Nreg:
		node->d_dir.d_type = DT_REG;
		break;

	case Ndev:
		if (dev != NULL) {
			node->d_dir.d_type = DT_CHR;
			node->d_dev = dev;

			node->mode = dev->si_perms;
			node->uid = dev->si_uid;
			node->gid = dev->si_gid;

			devfs_alias_check_create(node);
		}
		break;

	default:
		panic("devfs_allocp: unknown node type");
	}

	node->v_node = NULL;
	node->node_type = devfsnodetype;

	/* Initialize the dirent structure of each devfs vnode */
	node->d_dir.d_namlen = namlen;
	node->d_dir.d_name = kmalloc(namlen+1, M_DEVFS, M_WAITOK);
	memcpy(node->d_dir.d_name, name, namlen);
	node->d_dir.d_name[namlen] = '\0';

	/* Initialize the parent node element */
	node->parent = parent;

	/* Initialize *time members */
	nanotime(&node->atime);
	node->mtime = node->ctime = node->atime;

	/*
	 * Associate with parent as last step, clean out namecache
	 * reference.
	 */
	if ((parent != NULL) &&
	    ((parent->node_type == Nroot) || (parent->node_type == Ndir))) {
		parent->nchildren++;
		node->cookie = parent->cookie_jar++;
		node->flags |= DEVFS_NODE_LINKED;
		TAILQ_INSERT_TAIL(DEVFS_DENODE_HEAD(parent), node, link);

		/* This forces negative namecache lookups to clear */
		++mp->mnt_namecache_gen;
	}

	/* Apply rules */
	devfs_rule_check_apply(node, NULL);

	atomic_add_long(&DEVFS_MNTDATA(mp)->file_count, 1);

	return node;
}

/*
 * devfs_allocv() allocates a new vnode based on a devfs node.
 */
int
devfs_allocv(struct vnode **vpp, struct devfs_node *node)
{
	struct vnode *vp;
	int error = 0;

	KKASSERT(node);

	/*
	 * devfs master lock must not be held across a vget() call, we have
	 * to hold our ad-hoc vp to avoid a free race from destroying the
	 * contents of the structure.  The vget() will interlock recycles
	 * for us.
	 */
try_again:
	while ((vp = node->v_node) != NULL) {
		vhold(vp);
		lockmgr(&devfs_lock, LK_RELEASE);
		error = vget(vp, LK_EXCLUSIVE);
		vdrop(vp);
		lockmgr(&devfs_lock, LK_EXCLUSIVE);
		if (error == 0) {
			*vpp = vp;
			goto out;
		}
		if (error != ENOENT) {
			*vpp = NULL;
			goto out;
		}
	}

	/*
	 * devfs master lock must not be held across a getnewvnode() call.
	 */
	lockmgr(&devfs_lock, LK_RELEASE);
	if ((error = getnewvnode(VT_DEVFS, node->mp, vpp, 0, 0)) != 0) {
		lockmgr(&devfs_lock, LK_EXCLUSIVE);
		goto out;
	}
	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	vp = *vpp;

	if (node->v_node != NULL) {
		vp->v_type = VBAD;
		vx_put(vp);
		goto try_again;
	}

	vp->v_data = node;
	node->v_node = vp;

	switch (node->node_type) {
	case Nroot:
		vsetflags(vp, VROOT);
		/* fall through */
	case Ndir:
		vp->v_type = VDIR;
		break;

	case Nlink:
		vp->v_type = VLNK;
		break;

	case Nreg:
		vp->v_type = VREG;
		break;

	case Ndev:
		vp->v_type = VCHR;
		KKASSERT(node->d_dev);

		vp->v_uminor = node->d_dev->si_uminor;
		vp->v_umajor = node->d_dev->si_umajor;

		v_associate_rdev(vp, node->d_dev);
		vp->v_ops = &node->mp->mnt_vn_spec_ops;
		break;

	default:
		panic("devfs_allocv: unknown node type");
	}

out:
	return error;
}

/*
 * devfs_allocvp allocates both a devfs node (with the given settings) and a vnode
 * based on the newly created devfs node.
 */
int
devfs_allocvp(struct mount *mp, struct vnode **vpp, devfs_nodetype devfsnodetype,
		char *name, struct devfs_node *parent, cdev_t dev)
{
	struct devfs_node *node;

	node = devfs_allocp(devfsnodetype, name, parent, mp, dev);

	if (node != NULL)
		devfs_allocv(vpp, node);
	else
		*vpp = NULL;

	return 0;
}

/*
 * Destroy the devfs_node.  The node must be unlinked from the topology.
 *
 * This function will also destroy any vnode association with the node
 * and device.
 *
 * The cdev_t itself remains intact.
 *
 * The core lock is not necessarily held on call and must be temporarily
 * released if it is to avoid a deadlock.
 */
int
devfs_freep(struct devfs_node *node)
{
	struct vnode *vp;
	int relock;

	KKASSERT(node);
	KKASSERT(((node->flags & DEVFS_NODE_LINKED) == 0) ||
		 (node->node_type == Nroot));

	/*
	 * Protect against double frees
	 */
	KKASSERT((node->flags & DEVFS_DESTROYED) == 0);
	node->flags |= DEVFS_DESTROYED;

	/*
	 * Avoid deadlocks between devfs_lock and the vnode lock when
	 * disassociating the vnode (stress2 pty vs ls -la /dev/pts).
	 *
	 * This also prevents the vnode reclaim code from double-freeing
	 * the node.  The vget() is required to safely modified the vp
	 * and cycle the refs to terminate an inactive vp.
	 */
	if (lockstatus(&devfs_lock, curthread) == LK_EXCLUSIVE) {
		lockmgr(&devfs_lock, LK_RELEASE);
		relock = 1;
	} else {
		relock = 0;
	}

	while ((vp = node->v_node) != NULL) {
		if (vget(vp, LK_EXCLUSIVE | LK_RETRY) != 0)
			break;
		v_release_rdev(vp);
		vp->v_data = NULL;
		node->v_node = NULL;
		cache_inval_vp(vp, CINV_DESTROY);
		vput(vp);
	}

	/*
	 * Remaining cleanup
	 */
	atomic_subtract_long(&DEVFS_MNTDATA(node->mp)->leak_count, 1);
	if (node->symlink_name)	{
		kfree(node->symlink_name, M_DEVFS);
		node->symlink_name = NULL;
	}

	/*
	 * Remove the node from the orphan list if it is still on it.
	 */
	if (node->flags & DEVFS_ORPHANED)
		devfs_tracer_del_orphan(node);

	if (node->d_dir.d_name) {
		kfree(node->d_dir.d_name, M_DEVFS);
		node->d_dir.d_name = NULL;
	}
	atomic_subtract_long(&DEVFS_MNTDATA(node->mp)->file_count, 1);
	objcache_put(devfs_node_cache, node);

	if (relock)
		lockmgr(&devfs_lock, LK_EXCLUSIVE);

	return 0;
}

/*
 * Unlink the devfs node from the topology and add it to the orphan list.
 * The node will later be destroyed by freep.
 *
 * Any vnode association, including the v_rdev and v_data, remains intact
 * until the freep.
 */
int
devfs_unlinkp(struct devfs_node *node)
{
	struct devfs_node *parent;
	KKASSERT(node);

	/*
	 * Add the node to the orphan list, so it is referenced somewhere, to
	 * so we don't leak it.
	 */
	devfs_tracer_add_orphan(node);

	parent = node->parent;

	/*
	 * If the parent is known we can unlink the node out of the topology
	 */
	if (parent)	{
		TAILQ_REMOVE(DEVFS_DENODE_HEAD(parent), node, link);
		parent->nchildren--;
		node->flags &= ~DEVFS_NODE_LINKED;
	}

	node->parent = NULL;
	return 0;
}

void *
devfs_iterate_topology(struct devfs_node *node,
		devfs_iterate_callback_t *callback, void *arg1)
{
	struct devfs_node *node1, *node2;
	void *ret = NULL;

	if ((node->node_type == Nroot) || (node->node_type == Ndir)) {
		if (node->nchildren > 2) {
			TAILQ_FOREACH_MUTABLE(node1, DEVFS_DENODE_HEAD(node),
							link, node2) {
				if ((ret = devfs_iterate_topology(node1, callback, arg1)))
					return ret;
			}
		}
	}

	ret = callback(node, arg1);
	return ret;
}

/*
 * devfs_reaperp() is a recursive function that iterates through all the
 * topology, unlinking and freeing all devfs nodes.
 */
static void *
devfs_reaperp_callback(struct devfs_node *node, void *unused)
{
	devfs_unlinkp(node);
	devfs_freep(node);

	return NULL;
}

static void *
devfs_gc_dirs_callback(struct devfs_node *node, void *unused)
{
	if (node->node_type == Ndir) {
		if ((node->nchildren == 2) &&
		    !(node->flags & DEVFS_USER_CREATED)) {
			devfs_unlinkp(node);
			devfs_freep(node);
		}
	}

	return NULL;
}

static void *
devfs_gc_links_callback(struct devfs_node *node, struct devfs_node *target)
{
	if ((node->node_type == Nlink) && (node->link_target == target)) {
		devfs_unlinkp(node);
		devfs_freep(node);
	}

	return NULL;
}

/*
 * devfs_gc() is devfs garbage collector. It takes care of unlinking and
 * freeing a node, but also removes empty directories and links that link
 * via devfs auto-link mechanism to the node being deleted.
 */
int
devfs_gc(struct devfs_node *node)
{
	struct devfs_node *root_node = DEVFS_MNTDATA(node->mp)->root_node;

	if (node->nlinks > 0)
		devfs_iterate_topology(root_node,
				(devfs_iterate_callback_t *)devfs_gc_links_callback, node);

	devfs_unlinkp(node);
	devfs_iterate_topology(root_node,
			(devfs_iterate_callback_t *)devfs_gc_dirs_callback, NULL);

	devfs_freep(node);

	return 0;
}

/*
 * devfs_create_dev() is the asynchronous entry point for device creation.
 * It just sends a message with the relevant details to the devfs core.
 *
 * This function will reference the passed device.  The reference is owned
 * by devfs and represents all of the device's node associations.
 */
int
devfs_create_dev(cdev_t dev, uid_t uid, gid_t gid, int perms)
{
	reference_dev(dev);
	devfs_msg_send_dev(DEVFS_DEVICE_CREATE, dev, uid, gid, perms);

	return 0;
}

/*
 * devfs_destroy_dev() is the asynchronous entry point for device destruction.
 * It just sends a message with the relevant details to the devfs core.
 */
int
devfs_destroy_dev(cdev_t dev)
{
	devfs_msg_send_dev(DEVFS_DEVICE_DESTROY, dev, 0, 0, 0);
	return 0;
}

/*
 * devfs_mount_add() is the synchronous entry point for adding a new devfs
 * mount.  It sends a synchronous message with the relevant details to the
 * devfs core.
 */
int
devfs_mount_add(struct devfs_mnt_data *mnt)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_mnt = mnt;
	msg = devfs_msg_send_sync(DEVFS_MOUNT_ADD, msg);
	devfs_msg_put(msg);

	return 0;
}

/*
 * devfs_mount_del() is the synchronous entry point for removing a devfs mount.
 * It sends a synchronous message with the relevant details to the devfs core.
 */
int
devfs_mount_del(struct devfs_mnt_data *mnt)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_mnt = mnt;
	msg = devfs_msg_send_sync(DEVFS_MOUNT_DEL, msg);
	devfs_msg_put(msg);

	return 0;
}

/*
 * devfs_destroy_related() is the synchronous entry point for device
 * destruction by subname. It just sends a message with the relevant details to
 * the devfs core.
 */
int
devfs_destroy_related(cdev_t dev)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_load = dev;
	msg = devfs_msg_send_sync(DEVFS_DESTROY_RELATED, msg);
	devfs_msg_put(msg);
	return 0;
}

int
devfs_clr_related_flag(cdev_t dev, uint32_t flag)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_flags.dev = dev;
	msg->mdv_flags.flag = flag;
	msg = devfs_msg_send_sync(DEVFS_CLR_RELATED_FLAG, msg);
	devfs_msg_put(msg);

	return 0;
}

int
devfs_destroy_related_without_flag(cdev_t dev, uint32_t flag)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_flags.dev = dev;
	msg->mdv_flags.flag = flag;
	msg = devfs_msg_send_sync(DEVFS_DESTROY_RELATED_WO_FLAG, msg);
	devfs_msg_put(msg);

	return 0;
}

/*
 * devfs_create_all_dev is the asynchronous entry point to trigger device
 * node creation.  It just sends a message with the relevant details to
 * the devfs core.
 */
int
devfs_create_all_dev(struct devfs_node *root)
{
	devfs_msg_send_generic(DEVFS_CREATE_ALL_DEV, root);
	return 0;
}

/*
 * devfs_destroy_dev_by_ops is the asynchronous entry point to destroy all
 * devices with a specific set of dev_ops and minor.  It just sends a
 * message with the relevant details to the devfs core.
 */
int
devfs_destroy_dev_by_ops(struct dev_ops *ops, int minor)
{
	devfs_msg_send_ops(DEVFS_DESTROY_DEV_BY_OPS, ops, minor);
	return 0;
}

/*
 * devfs_clone_handler_add is the synchronous entry point to add a new
 * clone handler.  It just sends a message with the relevant details to
 * the devfs core.
 */
int
devfs_clone_handler_add(const char *name, d_clone_t *nhandler)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_chandler.name = name;
	msg->mdv_chandler.nhandler = nhandler;
	msg = devfs_msg_send_sync(DEVFS_CHANDLER_ADD, msg);
	devfs_msg_put(msg);
	return 0;
}

/*
 * devfs_clone_handler_del is the synchronous entry point to remove a
 * clone handler.  It just sends a message with the relevant details to
 * the devfs core.
 */
int
devfs_clone_handler_del(const char *name)
{
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_chandler.name = name;
	msg->mdv_chandler.nhandler = NULL;
	msg = devfs_msg_send_sync(DEVFS_CHANDLER_DEL, msg);
	devfs_msg_put(msg);
	return 0;
}

/*
 * devfs_find_device_by_name is the synchronous entry point to find a
 * device given its name.  It sends a synchronous message with the
 * relevant details to the devfs core and returns the answer.
 */
cdev_t
devfs_find_device_by_name(const char *fmt, ...)
{
	cdev_t found = NULL;
	devfs_msg_t msg;
	char *target;
	__va_list ap;

	if (fmt == NULL)
		return NULL;

	__va_start(ap, fmt);
	kvasnrprintf(&target, PATH_MAX, 10, fmt, ap);
	__va_end(ap);

	msg = devfs_msg_get();
	msg->mdv_name = target;
	msg = devfs_msg_send_sync(DEVFS_FIND_DEVICE_BY_NAME, msg);
	found = msg->mdv_cdev;
	devfs_msg_put(msg);
	kvasfree(&target);

	return found;
}

/*
 * devfs_find_device_by_udev is the synchronous entry point to find a
 * device given its udev number.  It sends a synchronous message with
 * the relevant details to the devfs core and returns the answer.
 */
cdev_t
devfs_find_device_by_udev(udev_t udev)
{
	cdev_t found = NULL;
	devfs_msg_t msg;

	msg = devfs_msg_get();
	msg->mdv_udev = udev;
	msg = devfs_msg_send_sync(DEVFS_FIND_DEVICE_BY_UDEV, msg);
	found = msg->mdv_cdev;
	devfs_msg_put(msg);

	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "devfs_find_device_by_udev found? %s  -end:3-\n",
		    ((found) ? found->si_name:"NO"));
	return found;
}

struct vnode *
devfs_inode_to_vnode(struct mount *mp, ino_t target)
{
	struct vnode *vp = NULL;
	devfs_msg_t msg;

	if (mp == NULL)
		return NULL;

	msg = devfs_msg_get();
	msg->mdv_ino.mp = mp;
	msg->mdv_ino.ino = target;
	msg = devfs_msg_send_sync(DEVFS_INODE_TO_VNODE, msg);
	vp = msg->mdv_ino.vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	devfs_msg_put(msg);

	return vp;
}

/*
 * devfs_make_alias is the asynchronous entry point to register an alias
 * for a device.  It just sends a message with the relevant details to the
 * devfs core.
 */
int
devfs_make_alias(const char *name, cdev_t dev_target)
{
	struct devfs_alias *alias;
	size_t len;

	len = strlen(name);

	alias = kmalloc(sizeof(struct devfs_alias), M_DEVFS, M_WAITOK);
	alias->name = kstrdup(name, M_DEVFS);
	alias->namlen = len;
	alias->dev_target = dev_target;

	devfs_msg_send_generic(DEVFS_MAKE_ALIAS, alias);
	return 0;
}

/*
 * devfs_destroy_alias is the asynchronous entry point to deregister an alias
 * for a device.  It just sends a message with the relevant details to the
 * devfs core.
 */
int
devfs_destroy_alias(const char *name, cdev_t dev_target)
{
	struct devfs_alias *alias;
	size_t len;

	len = strlen(name);

	alias = kmalloc(sizeof(struct devfs_alias), M_DEVFS, M_WAITOK);
	alias->name = kstrdup(name, M_DEVFS);
	alias->namlen = len;
	alias->dev_target = dev_target;

	devfs_msg_send_generic(DEVFS_DESTROY_ALIAS, alias);
	return 0;
}

/*
 * devfs_apply_rules is the asynchronous entry point to trigger application
 * of all rules.  It just sends a message with the relevant details to the
 * devfs core.
 */
int
devfs_apply_rules(char *mntto)
{
	char *new_name;

	new_name = kstrdup(mntto, M_DEVFS);
	devfs_msg_send_name(DEVFS_APPLY_RULES, new_name);

	return 0;
}

/*
 * devfs_reset_rules is the asynchronous entry point to trigger reset of all
 * rules. It just sends a message with the relevant details to the devfs core.
 */
int
devfs_reset_rules(char *mntto)
{
	char *new_name;

	new_name = kstrdup(mntto, M_DEVFS);
	devfs_msg_send_name(DEVFS_RESET_RULES, new_name);

	return 0;
}


/*
 * devfs_scan_callback is the asynchronous entry point to call a callback
 * on all cdevs.
 * It just sends a message with the relevant details to the devfs core.
 */
int
devfs_scan_callback(devfs_scan_t *callback, void *arg)
{
	devfs_msg_t msg;

	KKASSERT(callback);

	msg = devfs_msg_get();
	msg->mdv_load = callback;
	msg->mdv_load2 = arg;
	msg = devfs_msg_send_sync(DEVFS_SCAN_CALLBACK, msg);
	devfs_msg_put(msg);

	return 0;
}


/*
 * Acts as a message drain. Any message that is replied to here gets destroyed
 * and the memory freed.
 */
static void
devfs_msg_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
	devfs_msg_put((devfs_msg_t)msg);
}

/*
 * devfs_msg_get allocates a new devfs msg and returns it.
 */
devfs_msg_t
devfs_msg_get(void)
{
	return objcache_get(devfs_msg_cache, M_WAITOK);
}

/*
 * devfs_msg_put deallocates a given devfs msg.
 */
int
devfs_msg_put(devfs_msg_t msg)
{
	objcache_put(devfs_msg_cache, msg);
	return 0;
}

/*
 * devfs_msg_send is the generic asynchronous message sending facility
 * for devfs. By default the reply port is the automatic disposal port.
 *
 * If the current thread is the devfs_msg_port thread we execute the
 * operation synchronously.
 */
void
devfs_msg_send(uint32_t cmd, devfs_msg_t devfs_msg)
{
	lwkt_port_t port = &devfs_msg_port;

	lwkt_initmsg(&devfs_msg->hdr, &devfs_dispose_port, 0);

	devfs_msg->hdr.u.ms_result = cmd;

	if (port->mpu_td == curthread) {
		devfs_msg_exec(devfs_msg);
		lwkt_replymsg(&devfs_msg->hdr, 0);
	} else {
		lwkt_sendmsg(port, (lwkt_msg_t)devfs_msg);
	}
}

/*
 * devfs_msg_send_sync is the generic synchronous message sending
 * facility for devfs. It initializes a local reply port and waits
 * for the core's answer. This answer is then returned.
 */
devfs_msg_t
devfs_msg_send_sync(uint32_t cmd, devfs_msg_t devfs_msg)
{
	struct lwkt_port rep_port;
	devfs_msg_t	msg_incoming;
	lwkt_port_t port = &devfs_msg_port;

	lwkt_initport_thread(&rep_port, curthread);
	lwkt_initmsg(&devfs_msg->hdr, &rep_port, 0);

	devfs_msg->hdr.u.ms_result = cmd;

	lwkt_sendmsg(port, (lwkt_msg_t)devfs_msg);
	msg_incoming = lwkt_waitport(&rep_port, 0);

	return msg_incoming;
}

/*
 * sends a message with a generic argument.
 */
void
devfs_msg_send_generic(uint32_t cmd, void *load)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_load = load;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with a name argument.
 */
void
devfs_msg_send_name(uint32_t cmd, char *name)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_name = name;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with a mount argument.
 */
void
devfs_msg_send_mount(uint32_t cmd, struct devfs_mnt_data *mnt)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_mnt = mnt;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with an ops argument.
 */
void
devfs_msg_send_ops(uint32_t cmd, struct dev_ops *ops, int minor)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_ops.ops = ops;
	devfs_msg->mdv_ops.minor = minor;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with a clone handler argument.
 */
void
devfs_msg_send_chandler(uint32_t cmd, char *name, d_clone_t handler)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_chandler.name = name;
	devfs_msg->mdv_chandler.nhandler = handler;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with a device argument.
 */
void
devfs_msg_send_dev(uint32_t cmd, cdev_t dev, uid_t uid, gid_t gid, int perms)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_dev.dev = dev;
	devfs_msg->mdv_dev.uid = uid;
	devfs_msg->mdv_dev.gid = gid;
	devfs_msg->mdv_dev.perms = perms;

	devfs_msg_send(cmd, devfs_msg);
}

/*
 * sends a message with a link argument.
 */
void
devfs_msg_send_link(uint32_t cmd, char *name, char *target, struct mount *mp)
{
	devfs_msg_t devfs_msg = devfs_msg_get();

	devfs_msg->mdv_link.name = name;
	devfs_msg->mdv_link.target = target;
	devfs_msg->mdv_link.mp = mp;
	devfs_msg_send(cmd, devfs_msg);
}

/*
 * devfs_msg_core is the main devfs thread. It handles all incoming messages
 * and calls the relevant worker functions. By using messages it's assured
 * that events occur in the correct order.
 */
static void
devfs_msg_core(void *arg)
{
	devfs_msg_t msg;

	lwkt_initport_thread(&devfs_msg_port, curthread);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	devfs_run = 1;
	wakeup(td_core);
	lockmgr(&devfs_lock, LK_RELEASE);

	get_mplock();	/* mpsafe yet? */

	while (devfs_run) {
		msg = (devfs_msg_t)lwkt_waitport(&devfs_msg_port, 0);
		devfs_debug(DEVFS_DEBUG_DEBUG,
				"devfs_msg_core, new msg: %x\n",
				(unsigned int)msg->hdr.u.ms_result);
		devfs_msg_exec(msg);
		lwkt_replymsg(&msg->hdr, 0);
	}

	rel_mplock();
	wakeup(td_core);

	lwkt_exit();
}

static void
devfs_msg_exec(devfs_msg_t msg)
{
	struct devfs_mnt_data *mnt;
	struct devfs_node *node;
	cdev_t	dev;

	/*
	 * Acquire the devfs lock to ensure safety of all called functions
	 */
	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	switch (msg->hdr.u.ms_result) {
	case DEVFS_DEVICE_CREATE:
		dev = msg->mdv_dev.dev;
		devfs_create_dev_worker(dev,
					msg->mdv_dev.uid,
					msg->mdv_dev.gid,
					msg->mdv_dev.perms);
		break;
	case DEVFS_DEVICE_DESTROY:
		dev = msg->mdv_dev.dev;
		devfs_destroy_dev_worker(dev);
		break;
	case DEVFS_DESTROY_RELATED:
		devfs_destroy_related_worker(msg->mdv_load);
		break;
	case DEVFS_DESTROY_DEV_BY_OPS:
		devfs_destroy_dev_by_ops_worker(msg->mdv_ops.ops,
						msg->mdv_ops.minor);
		break;
	case DEVFS_CREATE_ALL_DEV:
		node = (struct devfs_node *)msg->mdv_load;
		devfs_create_all_dev_worker(node);
		break;
	case DEVFS_MOUNT_ADD:
		mnt = msg->mdv_mnt;
		TAILQ_INSERT_TAIL(&devfs_mnt_list, mnt, link);
		devfs_create_all_dev_worker(mnt->root_node);
		break;
	case DEVFS_MOUNT_DEL:
		mnt = msg->mdv_mnt;
		TAILQ_REMOVE(&devfs_mnt_list, mnt, link);
		devfs_iterate_topology(mnt->root_node, devfs_reaperp_callback,
				       NULL);
		if (mnt->leak_count) {
			devfs_debug(DEVFS_DEBUG_SHOW,
				    "Leaked %ld devfs_node elements!\n",
				    mnt->leak_count);
		}
		break;
	case DEVFS_CHANDLER_ADD:
		devfs_chandler_add_worker(msg->mdv_chandler.name,
				msg->mdv_chandler.nhandler);
		break;
	case DEVFS_CHANDLER_DEL:
		devfs_chandler_del_worker(msg->mdv_chandler.name);
		break;
	case DEVFS_FIND_DEVICE_BY_NAME:
		devfs_find_device_by_name_worker(msg);
		break;
	case DEVFS_FIND_DEVICE_BY_UDEV:
		devfs_find_device_by_udev_worker(msg);
		break;
	case DEVFS_MAKE_ALIAS:
		devfs_make_alias_worker((struct devfs_alias *)msg->mdv_load);
		break;
	case DEVFS_DESTROY_ALIAS:
		devfs_destroy_alias_worker((struct devfs_alias *)msg->mdv_load);
		break;
	case DEVFS_APPLY_RULES:
		devfs_apply_reset_rules_caller(msg->mdv_name, 1);
		break;
	case DEVFS_RESET_RULES:
		devfs_apply_reset_rules_caller(msg->mdv_name, 0);
		break;
	case DEVFS_SCAN_CALLBACK:
		devfs_scan_callback_worker((devfs_scan_t *)msg->mdv_load,
			msg->mdv_load2);
		break;
	case DEVFS_CLR_RELATED_FLAG:
		devfs_clr_related_flag_worker(msg->mdv_flags.dev,
				msg->mdv_flags.flag);
		break;
	case DEVFS_DESTROY_RELATED_WO_FLAG:
		devfs_destroy_related_without_flag_worker(msg->mdv_flags.dev,
				msg->mdv_flags.flag);
		break;
	case DEVFS_INODE_TO_VNODE:
		msg->mdv_ino.vp = devfs_iterate_topology(
			DEVFS_MNTDATA(msg->mdv_ino.mp)->root_node,
			(devfs_iterate_callback_t *)devfs_inode_to_vnode_worker_callback,
			&msg->mdv_ino.ino);
		break;
	case DEVFS_TERMINATE_CORE:
		devfs_run = 0;
		break;
	case DEVFS_SYNC:
		break;
	default:
		devfs_debug(DEVFS_DEBUG_WARNING,
			    "devfs_msg_core: unknown message "
			    "received at core\n");
		break;
	}
	lockmgr(&devfs_lock, LK_RELEASE);
}

/*
 * Worker function to insert a new dev into the dev list and initialize its
 * permissions. It also calls devfs_propagate_dev which in turn propagates
 * the change to all mount points.
 *
 * The passed dev is already referenced.  This reference is eaten by this
 * function and represents the dev's linkage into devfs_dev_list.
 */
static int
devfs_create_dev_worker(cdev_t dev, uid_t uid, gid_t gid, int perms)
{
	KKASSERT(dev);

	dev->si_uid = uid;
	dev->si_gid = gid;
	dev->si_perms = perms;

	devfs_link_dev(dev);
	devfs_propagate_dev(dev, 1);

	udev_event_attach(dev, NULL, 0);

	return 0;
}

/*
 * Worker function to delete a dev from the dev list and free the cdev.
 * It also calls devfs_propagate_dev which in turn propagates the change
 * to all mount points.
 */
static int
devfs_destroy_dev_worker(cdev_t dev)
{
	int error;

	KKASSERT(dev);
	KKASSERT((lockstatus(&devfs_lock, curthread)) == LK_EXCLUSIVE);

	error = devfs_unlink_dev(dev);
	devfs_propagate_dev(dev, 0);

	udev_event_detach(dev, NULL, 0);

	if (error == 0)
		release_dev(dev);	/* link ref */
	release_dev(dev);
	release_dev(dev);

	return 0;
}

/*
 * Worker function to destroy all devices with a certain basename.
 * Calls devfs_destroy_dev_worker for the actual destruction.
 */
static int
devfs_destroy_related_worker(cdev_t needle)
{
	cdev_t dev;

restart:
	devfs_debug(DEVFS_DEBUG_DEBUG, "related worker: %s\n",
	    needle->si_name);
	TAILQ_FOREACH(dev, &devfs_dev_list, link) {
		if (dev->si_parent == needle) {
			devfs_destroy_related_worker(dev);
			devfs_destroy_dev_worker(dev);
			goto restart;
		}
	}
	return 0;
}

static int
devfs_clr_related_flag_worker(cdev_t needle, uint32_t flag)
{
	cdev_t dev, dev1;

	TAILQ_FOREACH_MUTABLE(dev, &devfs_dev_list, link, dev1) {
		if (dev->si_parent == needle) {
			devfs_clr_related_flag_worker(dev, flag);
			dev->si_flags &= ~flag;
		}
	}

	return 0;
}

static int
devfs_destroy_related_without_flag_worker(cdev_t needle, uint32_t flag)
{
	cdev_t dev;

restart:
	devfs_debug(DEVFS_DEBUG_DEBUG, "related_wo_flag: %s\n",
	    needle->si_name);

	TAILQ_FOREACH(dev, &devfs_dev_list, link) {
		if (dev->si_parent == needle) {
			devfs_destroy_related_without_flag_worker(dev, flag);
			if (!(dev->si_flags & flag)) {
				devfs_destroy_dev_worker(dev);
				devfs_debug(DEVFS_DEBUG_DEBUG,
				    "related_wo_flag: %s restart\n", dev->si_name);
				goto restart;
			}
		}
	}

	return 0;
}

/*
 * Worker function that creates all device nodes on top of a devfs
 * root node.
 */
static int
devfs_create_all_dev_worker(struct devfs_node *root)
{
	cdev_t dev;

	KKASSERT(root);

	TAILQ_FOREACH(dev, &devfs_dev_list, link) {
		devfs_create_device_node(root, dev, NULL, NULL);
	}

	return 0;
}

/*
 * Worker function that destroys all devices that match a specific
 * dev_ops and/or minor. If minor is less than 0, it is not matched
 * against. It also propagates all changes.
 */
static int
devfs_destroy_dev_by_ops_worker(struct dev_ops *ops, int minor)
{
	cdev_t dev, dev1;

	KKASSERT(ops);

	TAILQ_FOREACH_MUTABLE(dev, &devfs_dev_list, link, dev1) {
		if (dev->si_ops != ops)
			continue;
		if ((minor < 0) || (dev->si_uminor == minor)) {
			devfs_destroy_dev_worker(dev);
		}
	}

	return 0;
}

/*
 * Worker function that registers a new clone handler in devfs.
 */
static int
devfs_chandler_add_worker(const char *name, d_clone_t *nhandler)
{
	struct devfs_clone_handler *chandler = NULL;
	u_char len = strlen(name);

	if (len == 0)
		return 1;

	TAILQ_FOREACH(chandler, &devfs_chandler_list, link) {
		if (chandler->namlen != len)
			continue;

		if (!memcmp(chandler->name, name, len)) {
			/* Clonable basename already exists */
			return 1;
		}
	}

	chandler = kmalloc(sizeof(*chandler), M_DEVFS, M_WAITOK | M_ZERO);
	chandler->name = kstrdup(name, M_DEVFS);
	chandler->namlen = len;
	chandler->nhandler = nhandler;

	TAILQ_INSERT_TAIL(&devfs_chandler_list, chandler, link);
	return 0;
}

/*
 * Worker function that removes a given clone handler from the
 * clone handler list.
 */
static int
devfs_chandler_del_worker(const char *name)
{
	struct devfs_clone_handler *chandler, *chandler2;
	u_char len = strlen(name);

	if (len == 0)
		return 1;

	TAILQ_FOREACH_MUTABLE(chandler, &devfs_chandler_list, link, chandler2) {
		if (chandler->namlen != len)
			continue;
		if (memcmp(chandler->name, name, len))
			continue;

		TAILQ_REMOVE(&devfs_chandler_list, chandler, link);
		kfree(chandler->name, M_DEVFS);
		kfree(chandler, M_DEVFS);
		break;
	}

	return 0;
}

/*
 * Worker function that finds a given device name and changes
 * the message received accordingly so that when replied to,
 * the answer is returned to the caller.
 */
static int
devfs_find_device_by_name_worker(devfs_msg_t devfs_msg)
{
	struct devfs_alias *alias;
	cdev_t dev;
	cdev_t found = NULL;

	TAILQ_FOREACH(dev, &devfs_dev_list, link) {
		if (strcmp(devfs_msg->mdv_name, dev->si_name) == 0) {
			found = dev;
			break;
		}
	}
	if (found == NULL) {
		TAILQ_FOREACH(alias, &devfs_alias_list, link) {
			if (strcmp(devfs_msg->mdv_name, alias->name) == 0) {
				found = alias->dev_target;
				break;
			}
		}
	}
	devfs_msg->mdv_cdev = found;

	return 0;
}

/*
 * Worker function that finds a given device udev and changes
 * the message received accordingly so that when replied to,
 * the answer is returned to the caller.
 */
static int
devfs_find_device_by_udev_worker(devfs_msg_t devfs_msg)
{
	cdev_t dev, dev1;
	cdev_t found = NULL;

	TAILQ_FOREACH_MUTABLE(dev, &devfs_dev_list, link, dev1) {
		if (((udev_t)dev->si_inode) == devfs_msg->mdv_udev) {
			found = dev;
			break;
		}
	}
	devfs_msg->mdv_cdev = found;

	return 0;
}

/*
 * Worker function that inserts a given alias into the
 * alias list, and propagates the alias to all mount
 * points.
 */
static int
devfs_make_alias_worker(struct devfs_alias *alias)
{
	struct devfs_alias *alias2;
	size_t len = strlen(alias->name);
	int found = 0;

	TAILQ_FOREACH(alias2, &devfs_alias_list, link) {
		if (len != alias2->namlen)
			continue;

		if (!memcmp(alias->name, alias2->name, len)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/*
		 * The alias doesn't exist yet, so we add it to the alias list
		 */
		TAILQ_INSERT_TAIL(&devfs_alias_list, alias, link);
		devfs_alias_propagate(alias, 0);
		udev_event_attach(alias->dev_target, alias->name, 1);
	} else {
		devfs_debug(DEVFS_DEBUG_WARNING,
			    "Warning: duplicate devfs_make_alias for %s\n",
			    alias->name);
		kfree(alias->name, M_DEVFS);
		kfree(alias, M_DEVFS);
	}

	return 0;
}

/*
 * Worker function that delete a given alias from the
 * alias list, and propagates the removal to all mount
 * points.
 */
static int
devfs_destroy_alias_worker(struct devfs_alias *alias)
{
	struct devfs_alias *alias2;
	int found = 0;

	TAILQ_FOREACH(alias2, &devfs_alias_list, link) {
		if (alias->dev_target != alias2->dev_target)
			continue;

		if (devfs_WildCmp(alias->name, alias2->name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found) {
		devfs_debug(DEVFS_DEBUG_WARNING,
		    "Warning: devfs_destroy_alias for inexistant alias: %s\n",
		    alias->name);
		kfree(alias->name, M_DEVFS);
		kfree(alias, M_DEVFS);
	} else {
		/*
		 * The alias exists, so we delete it from the alias list
		 */
		TAILQ_REMOVE(&devfs_alias_list, alias2, link);
		devfs_alias_propagate(alias2, 1);
		udev_event_detach(alias2->dev_target, alias2->name, 1);
		kfree(alias->name, M_DEVFS);
		kfree(alias, M_DEVFS);
		kfree(alias2->name, M_DEVFS);
		kfree(alias2, M_DEVFS);
	}

	return 0;
}

/*
 * Function that removes and frees all aliases.
 */
static int
devfs_alias_reap(void)
{
	struct devfs_alias *alias, *alias2;

	TAILQ_FOREACH_MUTABLE(alias, &devfs_alias_list, link, alias2) {
		TAILQ_REMOVE(&devfs_alias_list, alias, link);
		kfree(alias->name, M_DEVFS);
		kfree(alias, M_DEVFS);
	}
	return 0;
}

/*
 * Function that removes an alias matching a specific cdev and frees
 * it accordingly.
 */
static int
devfs_alias_remove(cdev_t dev)
{
	struct devfs_alias *alias, *alias2;

	TAILQ_FOREACH_MUTABLE(alias, &devfs_alias_list, link, alias2) {
		if (alias->dev_target == dev) {
			TAILQ_REMOVE(&devfs_alias_list, alias, link);
			udev_event_detach(alias->dev_target, alias->name, 1);
			kfree(alias->name, M_DEVFS);
			kfree(alias, M_DEVFS);
		}
	}
	return 0;
}

/*
 * This function propagates an alias addition or removal to
 * all mount points.
 */
static int
devfs_alias_propagate(struct devfs_alias *alias, int remove)
{
	struct devfs_mnt_data *mnt;

	TAILQ_FOREACH(mnt, &devfs_mnt_list, link) {
		if (remove) {
			devfs_destroy_node(mnt->root_node, alias->name);
		} else {
			devfs_alias_apply(mnt->root_node, alias);
		}
	}
	return 0;
}

/*
 * This function is a recursive function iterating through
 * all device nodes in the topology and, if applicable,
 * creating the relevant alias for a device node.
 */
static int
devfs_alias_apply(struct devfs_node *node, struct devfs_alias *alias)
{
	struct devfs_node *node1, *node2;

	KKASSERT(alias != NULL);

	if ((node->node_type == Nroot) || (node->node_type == Ndir)) {
		if (node->nchildren > 2) {
			TAILQ_FOREACH_MUTABLE(node1, DEVFS_DENODE_HEAD(node), link, node2) {
				devfs_alias_apply(node1, alias);
			}
		}
	} else {
		if (node->d_dev == alias->dev_target)
			devfs_alias_create(alias->name, node, 0);
	}
	return 0;
}

/*
 * This function checks if any alias possibly is applicable
 * to the given node. If so, the alias is created.
 */
static int
devfs_alias_check_create(struct devfs_node *node)
{
	struct devfs_alias *alias;

	TAILQ_FOREACH(alias, &devfs_alias_list, link) {
		if (node->d_dev == alias->dev_target)
			devfs_alias_create(alias->name, node, 0);
	}
	return 0;
}

/*
 * This function creates an alias with a given name
 * linking to a given devfs node. It also increments
 * the link count on the target node.
 */
int
devfs_alias_create(char *name_orig, struct devfs_node *target, int rule_based)
{
	struct mount *mp = target->mp;
	struct devfs_node *parent = DEVFS_MNTDATA(mp)->root_node;
	struct devfs_node *linknode;
	char *create_path = NULL;
	char *name;
	char *name_buf;
	int result = 0;

	KKASSERT((lockstatus(&devfs_lock, curthread)) == LK_EXCLUSIVE);

	name_buf = kmalloc(PATH_MAX, M_TEMP, M_WAITOK);
	devfs_resolve_name_path(name_orig, name_buf, &create_path, &name);

	if (create_path)
		parent = devfs_resolve_or_create_path(parent, create_path, 1);


	if (devfs_find_device_node_by_name(parent, name)) {
		devfs_debug(DEVFS_DEBUG_WARNING,
			    "Node already exists: %s "
			    "(devfs_make_alias_worker)!\n",
			    name);
		result = 1;
		goto done;
	}

	linknode = devfs_allocp(Nlink, name, parent, mp, NULL);
	if (linknode == NULL) {
		result = 1;
		goto done;
	}

	linknode->link_target = target;
	target->nlinks++;

	if (rule_based)
		linknode->flags |= DEVFS_RULE_CREATED;

done:
	kfree(name_buf, M_TEMP);
	return (result);
}

/*
 * This function is called by the core and handles mount point
 * strings. It either calls the relevant worker (devfs_apply_
 * reset_rules_worker) on all mountpoints or only a specific
 * one.
 */
static int
devfs_apply_reset_rules_caller(char *mountto, int apply)
{
	struct devfs_mnt_data *mnt;

	if (mountto[0] == '*') {
		TAILQ_FOREACH(mnt, &devfs_mnt_list, link) {
			devfs_iterate_topology(mnt->root_node,
					(apply)?(devfs_rule_check_apply):(devfs_rule_reset_node),
					NULL);
		}
	} else {
		TAILQ_FOREACH(mnt, &devfs_mnt_list, link) {
			if (!strcmp(mnt->mp->mnt_stat.f_mntonname, mountto)) {
				devfs_iterate_topology(mnt->root_node,
					(apply)?(devfs_rule_check_apply):(devfs_rule_reset_node),
					NULL);
				break;
			}
		}
	}

	kfree(mountto, M_DEVFS);
	return 0;
}

/*
 * This function calls a given callback function for
 * every dev node in the devfs dev list.
 */
static int
devfs_scan_callback_worker(devfs_scan_t *callback, void *arg)
{
	cdev_t dev, dev1;
	struct devfs_alias *alias, *alias1;

	TAILQ_FOREACH_MUTABLE(dev, &devfs_dev_list, link, dev1) {
		callback(dev->si_name, dev, false, arg);
	}
	TAILQ_FOREACH_MUTABLE(alias, &devfs_alias_list, link, alias1) {
		callback(alias->name, alias->dev_target, true, arg);
	}

	return 0;
}

/*
 * This function tries to resolve a given directory, or if not
 * found and creation requested, creates the given directory.
 */
static struct devfs_node *
devfs_resolve_or_create_dir(struct devfs_node *parent, char *dir_name,
			    size_t name_len, int create)
{
	struct devfs_node *node, *found = NULL;

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(parent), link) {
		if (name_len != node->d_dir.d_namlen)
			continue;

		if (!memcmp(dir_name, node->d_dir.d_name, name_len)) {
			found = node;
			break;
		}
	}

	if ((found == NULL) && (create)) {
		found = devfs_allocp(Ndir, dir_name, parent, parent->mp, NULL);
	}

	return found;
}

/*
 * This function tries to resolve a complete path. If creation is requested,
 * if a given part of the path cannot be resolved (because it doesn't exist),
 * it is created.
 */
struct devfs_node *
devfs_resolve_or_create_path(struct devfs_node *parent, char *path, int create)
{
	struct devfs_node *node = parent;
	char *buf;
	size_t idx = 0;

	if (path == NULL)
		return parent;

	buf = kmalloc(PATH_MAX, M_TEMP, M_WAITOK);

	while (*path && idx < PATH_MAX - 1) {
		if (*path != '/') {
			buf[idx++] = *path;
		} else {
			buf[idx] = '\0';
			node = devfs_resolve_or_create_dir(node, buf, idx, create);
			if (node == NULL) {
				kfree(buf, M_TEMP);
				return NULL;
			}
			idx = 0;
		}
		++path;
	}
	buf[idx] = '\0';
	node = devfs_resolve_or_create_dir(node, buf, idx, create);
	kfree (buf, M_TEMP);
	return (node);
}

/*
 * Takes a full path and strips it into a directory path and a name.
 * For a/b/c/foo, it returns foo in namep and a/b/c in pathp. It
 * requires a working buffer with enough size to keep the whole
 * fullpath.
 */
int
devfs_resolve_name_path(char *fullpath, char *buf, char **pathp, char **namep)
{
	char *name = NULL;
	char *path = NULL;
	size_t len = strlen(fullpath) + 1;
	int i;

	KKASSERT((fullpath != NULL) && (buf != NULL));
	KKASSERT((pathp != NULL) && (namep != NULL));

	memcpy(buf, fullpath, len);

	for (i = len-1; i>= 0; i--) {
		if (buf[i] == '/') {
			buf[i] = '\0';
			name = &(buf[i+1]);
			path = buf;
			break;
		}
	}

	*pathp = path;

	if (name) {
		*namep = name;
	} else {
		*namep = buf;
	}

	return 0;
}

/*
 * This function creates a new devfs node for a given device.  It can
 * handle a complete path as device name, and accordingly creates
 * the path and the final device node.
 *
 * The reference count on the passed dev remains unchanged.
 */
struct devfs_node *
devfs_create_device_node(struct devfs_node *root, cdev_t dev,
			 char *dev_name, char *path_fmt, ...)
{
	struct devfs_node *parent, *node = NULL;
	char *path = NULL;
	char *name;
	char *name_buf;
	__va_list ap;
	int i, found;
	char *create_path = NULL;
	char *names = "pqrsPQRS";

	name_buf = kmalloc(PATH_MAX, M_TEMP, M_WAITOK);

	if (path_fmt != NULL) {
		__va_start(ap, path_fmt);
		kvasnrprintf(&path, PATH_MAX, 10, path_fmt, ap);
		__va_end(ap);
	}

	parent = devfs_resolve_or_create_path(root, path, 1);
	KKASSERT(parent);

	devfs_resolve_name_path(
			((dev_name == NULL) && (dev))?(dev->si_name):(dev_name),
			name_buf, &create_path, &name);

	if (create_path)
		parent = devfs_resolve_or_create_path(parent, create_path, 1);


	if (devfs_find_device_node_by_name(parent, name)) {
		devfs_debug(DEVFS_DEBUG_WARNING, "devfs_create_device_node: "
			"DEVICE %s ALREADY EXISTS!!! Ignoring creation request.\n", name);
		goto out;
	}

	node = devfs_allocp(Ndev, name, parent, parent->mp, dev);
	nanotime(&parent->mtime);

	/*
	 * Ugly unix98 pty magic, to hide pty master (ptm) devices and their
	 * directory
	 */
	if ((dev) && (strlen(dev->si_name) >= 4) &&
			(!memcmp(dev->si_name, "ptm/", 4))) {
		node->parent->flags |= DEVFS_HIDDEN;
		node->flags |= DEVFS_HIDDEN;
	}

	/*
	 * Ugly pty magic, to tag pty devices as such and hide them if needed.
	 */
	if ((strlen(name) >= 3) && (!memcmp(name, "pty", 3)))
		node->flags |= (DEVFS_PTY | DEVFS_INVISIBLE);

	if ((strlen(name) >= 3) && (!memcmp(name, "tty", 3))) {
		found = 0;
		for (i = 0; i < strlen(names); i++) {
			if (name[3] == names[i]) {
				found = 1;
				break;
			}
		}
		if (found)
			node->flags |= (DEVFS_PTY | DEVFS_INVISIBLE);
	}

out:
	kfree(name_buf, M_TEMP);
	kvasfree(&path);
	return node;
}

/*
 * This function finds a given device node in the topology with a given
 * cdev.
 */
void *
devfs_find_device_node_callback(struct devfs_node *node, cdev_t target)
{
	if ((node->node_type == Ndev) && (node->d_dev == target)) {
		return node;
	}

	return NULL;
}

/*
 * This function finds a device node in the given parent directory by its
 * name and returns it.
 */
struct devfs_node *
devfs_find_device_node_by_name(struct devfs_node *parent, char *target)
{
	struct devfs_node *node, *found = NULL;
	size_t len = strlen(target);

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(parent), link) {
		if (len != node->d_dir.d_namlen)
			continue;

		if (!memcmp(node->d_dir.d_name, target, len)) {
			found = node;
			break;
		}
	}

	return found;
}

static void *
devfs_inode_to_vnode_worker_callback(struct devfs_node *node, ino_t *inop)
{
	struct vnode *vp = NULL;
	ino_t target = *inop;

	if (node->d_dir.d_ino == target) {
		if (node->v_node) {
			vp = node->v_node;
			vget(vp, LK_EXCLUSIVE | LK_RETRY);
			vn_unlock(vp);
		} else {
			devfs_allocv(&vp, node);
			vn_unlock(vp);
		}
	}

	return vp;
}

/*
 * This function takes a cdev and removes its devfs node in the
 * given topology.  The cdev remains intact.
 */
int
devfs_destroy_device_node(struct devfs_node *root, cdev_t target)
{
	KKASSERT(target != NULL);
	return devfs_destroy_node(root, target->si_name);
}

/*
 * This function takes a path to a devfs node, resolves it and
 * removes the devfs node from the given topology.
 */
int
devfs_destroy_node(struct devfs_node *root, char *target)
{
	struct devfs_node *node, *parent;
	char *name;
	char *name_buf;
	char *create_path = NULL;

	KKASSERT(target);

	name_buf = kmalloc(PATH_MAX, M_TEMP, M_WAITOK);
	ksnprintf(name_buf, PATH_MAX, "%s", target);

	devfs_resolve_name_path(target, name_buf, &create_path, &name);

	if (create_path)
		parent = devfs_resolve_or_create_path(root, create_path, 0);
	else
		parent = root;

	if (parent == NULL) {
		kfree(name_buf, M_TEMP);
		return 1;
	}

	node = devfs_find_device_node_by_name(parent, name);

	if (node) {
		nanotime(&node->parent->mtime);
		devfs_gc(node);
	}

	kfree(name_buf, M_TEMP);

	return 0;
}

/*
 * Just set perms and ownership for given node.
 */
int
devfs_set_perms(struct devfs_node *node, uid_t uid, gid_t gid,
		u_short mode, u_long flags)
{
	node->mode = mode;
	node->uid = uid;
	node->gid = gid;

	return 0;
}

/*
 * Propagates a device attach/detach to all mount
 * points. Also takes care of automatic alias removal
 * for a deleted cdev.
 */
static int
devfs_propagate_dev(cdev_t dev, int attach)
{
	struct devfs_mnt_data *mnt;

	TAILQ_FOREACH(mnt, &devfs_mnt_list, link) {
		if (attach) {
			/* Device is being attached */
			devfs_create_device_node(mnt->root_node, dev,
						 NULL, NULL );
		} else {
			/* Device is being detached */
			devfs_alias_remove(dev);
			devfs_destroy_device_node(mnt->root_node, dev);
		}
	}
	return 0;
}

/*
 * devfs_clone either returns a basename from a complete name by
 * returning the length of the name without trailing digits, or,
 * if clone != 0, calls the device's clone handler to get a new
 * device, which in turn is returned in devp.
 */
cdev_t
devfs_clone(cdev_t dev, const char *name, size_t len, int mode,
		struct ucred *cred)
{
	int error;
	struct devfs_clone_handler *chandler;
	struct dev_clone_args ap;

	TAILQ_FOREACH(chandler, &devfs_chandler_list, link) {
		if (chandler->namlen != len)
			continue;
		if ((!memcmp(chandler->name, name, len)) && (chandler->nhandler)) {
			lockmgr(&devfs_lock, LK_RELEASE);
			devfs_config();
			lockmgr(&devfs_lock, LK_EXCLUSIVE);

			ap.a_head.a_dev = dev;
			ap.a_dev = NULL;
			ap.a_name = name;
			ap.a_namelen = len;
			ap.a_mode = mode;
			ap.a_cred = cred;
			error = (chandler->nhandler)(&ap);
			if (error)
				continue;

			return ap.a_dev;
		}
	}

	return NULL;
}


/*
 * Registers a new orphan in the orphan list.
 */
void
devfs_tracer_add_orphan(struct devfs_node *node)
{
	struct devfs_orphan *orphan;

	KKASSERT(node);
	orphan = kmalloc(sizeof(struct devfs_orphan), M_DEVFS, M_WAITOK);
	orphan->node = node;

	KKASSERT((node->flags & DEVFS_ORPHANED) == 0);
	node->flags |= DEVFS_ORPHANED;
	TAILQ_INSERT_TAIL(DEVFS_ORPHANLIST(node->mp), orphan, link);
}

/*
 * Removes an orphan from the orphan list.
 */
void
devfs_tracer_del_orphan(struct devfs_node *node)
{
	struct devfs_orphan *orphan;

	KKASSERT(node);

	TAILQ_FOREACH(orphan, DEVFS_ORPHANLIST(node->mp), link)	{
		if (orphan->node == node) {
			node->flags &= ~DEVFS_ORPHANED;
			TAILQ_REMOVE(DEVFS_ORPHANLIST(node->mp), orphan, link);
			kfree(orphan, M_DEVFS);
			break;
		}
	}
}

/*
 * Counts the orphans in the orphan list, and if cleanup
 * is specified, also frees the orphan and removes it from
 * the list.
 */
size_t
devfs_tracer_orphan_count(struct mount *mp, int cleanup)
{
	struct devfs_orphan *orphan, *orphan2;
	size_t count = 0;

	TAILQ_FOREACH_MUTABLE(orphan, DEVFS_ORPHANLIST(mp), link, orphan2)	{
		count++;
		/*
		 * If we are instructed to clean up, we do so.
		 */
		if (cleanup) {
			TAILQ_REMOVE(DEVFS_ORPHANLIST(mp), orphan, link);
			orphan->node->flags &= ~DEVFS_ORPHANED;
			devfs_freep(orphan->node);
			kfree(orphan, M_DEVFS);
		}
	}

	return count;
}

/*
 * Fetch an ino_t from the global d_ino by increasing it
 * while spinlocked.
 */
static ino_t
devfs_fetch_ino(void)
{
	ino_t	ret;

	spin_lock(&ino_lock);
	ret = d_ino++;
	spin_unlock(&ino_lock);

	return ret;
}

/*
 * Allocates a new cdev and initializes it's most basic
 * fields.
 */
cdev_t
devfs_new_cdev(struct dev_ops *ops, int minor, struct dev_ops *bops)
{
	cdev_t dev = sysref_alloc(&cdev_sysref_class);

	sysref_activate(&dev->si_sysref);
	reference_dev(dev);
	bzero(dev, offsetof(struct cdev, si_sysref));

	dev->si_uid = 0;
	dev->si_gid = 0;
	dev->si_perms = 0;
	dev->si_drv1 = NULL;
	dev->si_drv2 = NULL;
	dev->si_lastread = 0;		/* time_uptime */
	dev->si_lastwrite = 0;		/* time_uptime */

	dev->si_dict = NULL;
	dev->si_parent = NULL;
	dev->si_ops = ops;
	dev->si_flags = 0;
	dev->si_uminor = minor;
	dev->si_bops = bops;

	/*
	 * Since the disk subsystem is in the way, we need to
	 * propagate the D_CANFREE from bops (and ops) to
	 * si_flags.
	 */
	if (bops && (bops->head.flags & D_CANFREE)) {
		dev->si_flags |= SI_CANFREE;
	} else if (ops->head.flags & D_CANFREE) {
		dev->si_flags |= SI_CANFREE;
	}

	/* If there is a backing device, we reference its ops */
	dev->si_inode = makeudev(
		    devfs_reference_ops((bops)?(bops):(ops)),
		    minor );
	dev->si_umajor = umajor(dev->si_inode);

	return dev;
}

static void
devfs_cdev_terminate(cdev_t dev)
{
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the devfs lock */
	if ((lockstatus(&devfs_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&devfs_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	/*
	 * Make sure the node isn't linked anymore. Otherwise we've screwed
	 * up somewhere, since normal devs are unlinked on the call to
	 * destroy_dev and only-cdevs that have not been used for cloning
	 * are not linked in the first place. only-cdevs used for cloning
	 * will be linked in, too, and should only be destroyed via
	 * destroy_dev, not destroy_only_dev, so we catch that problem, too.
	 */
	KKASSERT((dev->si_flags & SI_DEVFS_LINKED) == 0);

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&devfs_lock, LK_RELEASE);

	/* If there is a backing device, we release the backing device's ops */
	devfs_release_ops((dev->si_bops)?(dev->si_bops):(dev->si_ops));

	/* Finally destroy the device */
	sysref_put(&dev->si_sysref);
}

/*
 * Dummies for now (individual locks for MPSAFE)
 */
static void
devfs_cdev_lock(cdev_t dev)
{
}

static void
devfs_cdev_unlock(cdev_t dev)
{
}

static int
devfs_detached_filter_eof(struct knote *kn, long hint)
{
	kn->kn_flags |= (EV_EOF | EV_NODATA);
	return (1);
}

static void
devfs_detached_filter_detach(struct knote *kn)
{
	cdev_t dev = (cdev_t)kn->kn_hook;

	knote_remove(&dev->si_kqinfo.ki_note, kn);
}

static struct filterops devfs_detached_filterops =
	{ FILTEROP_ISFD, NULL,
	  devfs_detached_filter_detach,
	  devfs_detached_filter_eof };

/*
 * Delegates knote filter handling responsibility to devfs
 *
 * Any device that implements kqfilter event handling and could be detached
 * or shut down out from under the kevent subsystem must allow devfs to
 * assume responsibility for any knotes it may hold.
 */
void
devfs_assume_knotes(cdev_t dev, struct kqinfo *kqi)
{
	/*
	 * Let kern/kern_event.c do the heavy lifting.
	 */
	knote_assume_knotes(kqi, &dev->si_kqinfo,
			    &devfs_detached_filterops, (void *)dev);

	/*
	 * These should probably be activated individually, but doing so
	 * would require refactoring kq's public in-kernel interface.
	 */
	KNOTE(&dev->si_kqinfo.ki_note, 0);
}

/*
 * Links a given cdev into the dev list.
 */
int
devfs_link_dev(cdev_t dev)
{
	KKASSERT((dev->si_flags & SI_DEVFS_LINKED) == 0);
	dev->si_flags |= SI_DEVFS_LINKED;
	TAILQ_INSERT_TAIL(&devfs_dev_list, dev, link);

	return 0;
}

/*
 * Removes a given cdev from the dev list.  The caller is responsible for
 * releasing the reference on the device associated with the linkage.
 *
 * Returns EALREADY if the dev has already been unlinked.
 */
static int
devfs_unlink_dev(cdev_t dev)
{
	if ((dev->si_flags & SI_DEVFS_LINKED)) {
		TAILQ_REMOVE(&devfs_dev_list, dev, link);
		dev->si_flags &= ~SI_DEVFS_LINKED;
		return (0);
	}
	return (EALREADY);
}

int
devfs_node_is_accessible(struct devfs_node *node)
{
	if ((node) && (!(node->flags & DEVFS_HIDDEN)))
		return 1;
	else
		return 0;
}

int
devfs_reference_ops(struct dev_ops *ops)
{
	int unit;
	struct devfs_dev_ops *found = NULL;
	struct devfs_dev_ops *devops;

	TAILQ_FOREACH(devops, &devfs_dev_ops_list, link) {
		if (devops->ops == ops) {
			found = devops;
			break;
		}
	}

	if (!found) {
		found = kmalloc(sizeof(struct devfs_dev_ops), M_DEVFS, M_WAITOK);
		found->ops = ops;
		found->ref_count = 0;
		TAILQ_INSERT_TAIL(&devfs_dev_ops_list, found, link);
	}

	KKASSERT(found);

	if (found->ref_count == 0) {
		found->id = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(ops_id), 255);
		if (found->id == -1) {
			/* Ran out of unique ids */
			devfs_debug(DEVFS_DEBUG_WARNING,
					"devfs_reference_ops: WARNING: ran out of unique ids\n");
		}
	}
	unit = found->id;
	++found->ref_count;

	return unit;
}

void
devfs_release_ops(struct dev_ops *ops)
{
	struct devfs_dev_ops *found = NULL;
	struct devfs_dev_ops *devops;

	TAILQ_FOREACH(devops, &devfs_dev_ops_list, link) {
		if (devops->ops == ops) {
			found = devops;
			break;
		}
	}

	KKASSERT(found);

	--found->ref_count;

	if (found->ref_count == 0) {
		TAILQ_REMOVE(&devfs_dev_ops_list, found, link);
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(ops_id), found->id);
		kfree(found, M_DEVFS);
	}
}

/*
 * Wait for asynchronous messages to complete in the devfs helper
 * thread, then return.  Do nothing if the helper thread is dead
 * or we are being indirectly called from the helper thread itself.
 */
void
devfs_config(void)
{
	devfs_msg_t msg;

	if (devfs_run && curthread != td_core) {
		msg = devfs_msg_get();
		msg = devfs_msg_send_sync(DEVFS_SYNC, msg);
		devfs_msg_put(msg);
	}
}

/*
 * Called on init of devfs; creates the objcaches and
 * spawns off the devfs core thread. Also initializes
 * locks.
 */
static void
devfs_init(void)
{
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_init() called\n");
	/* Create objcaches for nodes, msgs and devs */
	devfs_node_cache = objcache_create("devfs-node-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &devfs_node_malloc_args );

	devfs_msg_cache = objcache_create("devfs-msg-cache", 0, 0,
					  NULL, NULL, NULL,
					  objcache_malloc_alloc,
					  objcache_malloc_free,
					  &devfs_msg_malloc_args );

	devfs_dev_cache = objcache_create("devfs-dev-cache", 0, 0,
					  NULL, NULL, NULL,
					  objcache_malloc_alloc,
					  objcache_malloc_free,
					  &devfs_dev_malloc_args );

	devfs_clone_bitmap_init(&DEVFS_CLONE_BITMAP(ops_id));

	/* Initialize the reply-only port which acts as a message drain */
	lwkt_initport_replyonly(&devfs_dispose_port, devfs_msg_autofree_reply);

	/* Initialize *THE* devfs lock */
	lockinit(&devfs_lock, "devfs_core lock", 0, 0);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	lwkt_create(devfs_msg_core, /*args*/NULL, &td_core, NULL,
		    0, -1, "devfs_msg_core");
	while (devfs_run == 0)
		lksleep(td_core, &devfs_lock, 0, "devfsc", 0);
	lockmgr(&devfs_lock, LK_RELEASE);

	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_init finished\n");
}

/*
 * Called on unload of devfs; takes care of destroying the core
 * and the objcaches. Also removes aliases that are no longer needed.
 */
static void
devfs_uninit(void)
{
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_uninit() called\n");

	devfs_msg_send(DEVFS_TERMINATE_CORE, NULL);
	while (devfs_run)
		tsleep(td_core, 0, "devfsc", hz*10);
	tsleep(td_core, 0, "devfsc", hz);

	devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(ops_id));

	/* Destroy the objcaches */
	objcache_destroy(devfs_msg_cache);
	objcache_destroy(devfs_node_cache);
	objcache_destroy(devfs_dev_cache);

	devfs_alias_reap();
}

/*
 * This is a sysctl handler to assist userland devname(3) to
 * find the device name for a given udev.
 */
static int
devfs_sysctl_devname_helper(SYSCTL_HANDLER_ARGS)
{
	udev_t 	udev;
	cdev_t	found;
	int		error;


	if ((error = SYSCTL_IN(req, &udev, sizeof(udev_t))))
		return (error);

	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs sysctl, received udev: %d\n", udev);

	if (udev == NOUDEV)
		return(EINVAL);

	if ((found = devfs_find_device_by_udev(udev)) == NULL)
		return(ENOENT);

	return(SYSCTL_OUT(req, found->si_name, strlen(found->si_name) + 1));
}


SYSCTL_PROC(_kern, OID_AUTO, devname, CTLTYPE_OPAQUE|CTLFLAG_RW|CTLFLAG_ANYBODY,
			NULL, 0, devfs_sysctl_devname_helper, "", "helper for devname(3)");

SYSCTL_NODE(_vfs, OID_AUTO, devfs, CTLFLAG_RW, 0, "devfs");
TUNABLE_INT("vfs.devfs.debug", &devfs_debug_enable);
SYSCTL_INT(_vfs_devfs, OID_AUTO, debug, CTLFLAG_RW, &devfs_debug_enable,
		0, "Enable DevFS debugging");

SYSINIT(vfs_devfs_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST,
		devfs_init, NULL);
SYSUNINIT(vfs_devfs_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY,
		devfs_uninit, NULL);

/*
 * WildCmp() - compare wild string to sane string
 *
 *	Returns 0 on success, -1 on failure.
 */
static int
wildCmp(const char **mary, int d, const char *w, const char *s)
{
    int i;

    /*
     * skip fixed portion
     */
    for (;;) {
	switch(*w) {
	case '*':
	    /*
	     * optimize terminator
	     */
	    if (w[1] == 0)
		return(0);
	    if (w[1] != '?' && w[1] != '*') {
		/*
		 * optimize * followed by non-wild
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (s[i] == w[1] && wildCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    } else {
		/*
		 * less-optimal
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (wildCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    }
	    mary[d] = s;
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s)
		return(-1);
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}


/*
 * WildCaseCmp() - compare wild string to sane string, case insensitive
 *
 *	Returns 0 on success, -1 on failure.
 */
static int
wildCaseCmp(const char **mary, int d, const char *w, const char *s)
{
    int i;

    /*
     * skip fixed portion
     */
    for (;;) {
	switch(*w) {
	case '*':
	    /*
	     * optimize terminator
	     */
	    if (w[1] == 0)
		return(0);
	    if (w[1] != '?' && w[1] != '*') {
		/*
		 * optimize * followed by non-wild
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (s[i] == w[1] && wildCaseCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    } else {
		/*
		 * less-optimal
		 */
		for (i = 0; s + i < mary[d]; ++i) {
		    if (wildCaseCmp(mary, d + 1, w + 1, s + i) == 0)
			return(0);
		}
	    }
	    mary[d] = s;
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s) {
#define tolower(x)	((x >= 'A' && x <= 'Z')?(x+('a'-'A')):(x))
		if (tolower(*w) != tolower(*s))
		    return(-1);
	    }
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}

struct cdev_privdata {
	void		*cdpd_data;
	cdevpriv_dtr_t	cdpd_dtr;
};

int devfs_get_cdevpriv(struct file *fp, void **datap)
{
	struct cdev_privdata *p;
	int error;

	if (fp == NULL)
		return(EBADF);
	p  = (struct cdev_privdata*) fp->f_data1;
	if (p != NULL) {
		error = 0;
		*datap = p->cdpd_data;
	} else
		error = ENOENT;
	return (error);
}

int devfs_set_cdevpriv(struct file *fp, void *priv, cdevpriv_dtr_t dtr)
{
	struct cdev_privdata *p;
	int error;

	if (fp == NULL)
		return (ENOENT);

	p = kmalloc(sizeof(struct cdev_privdata), M_DEVFS, M_WAITOK);
	p->cdpd_data = priv;
	p->cdpd_dtr = dtr;

	spin_lock(&fp->f_spin);
	if (fp->f_data1 == NULL) {
		fp->f_data1 = p;
		error = 0;
	} else
		error = EBUSY;
	spin_unlock(&fp->f_spin);

	if (error)
		kfree(p, M_DEVFS);

	return error;
}

void devfs_clear_cdevpriv(struct file *fp)
{
	struct cdev_privdata *p;

	if (fp == NULL)
		return;

	spin_lock(&fp->f_spin);
	p = fp->f_data1;
	fp->f_data1 = NULL;
	spin_unlock(&fp->f_spin);

	if (p != NULL) {
		(p->cdpd_dtr)(p->cdpd_data);
		kfree(p, M_DEVFS);
	}
}

int
devfs_WildCmp(const char *w, const char *s)
{
    int i;
    int c;
    int slen = strlen(s);
    const char **mary;

    for (i = c = 0; w[i]; ++i) {
	if (w[i] == '*')
	    ++c;
    }
    mary = kmalloc(sizeof(char *) * (c + 1), M_DEVFS, M_WAITOK);
    for (i = 0; i < c; ++i)
	mary[i] = s + slen;
    i = wildCmp(mary, 0, w, s);
    kfree(mary, M_DEVFS);
    return(i);
}

int
devfs_WildCaseCmp(const char *w, const char *s)
{
    int i;
    int c;
    int slen = strlen(s);
    const char **mary;

    for (i = c = 0; w[i]; ++i) {
	if (w[i] == '*')
	    ++c;
    }
    mary = kmalloc(sizeof(char *) * (c + 1), M_DEVFS, M_WAITOK);
    for (i = 0; i < c; ++i)
	mary[i] = s + slen;
    i = wildCaseCmp(mary, 0, w, s);
    kfree(mary, M_DEVFS);
    return(i);
}

