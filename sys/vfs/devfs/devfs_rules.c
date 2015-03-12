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
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/spinlock2.h>
#include <sys/fcntl.h>
#include <sys/device.h>
#include <sys/mount.h>
#include <sys/devfs.h>
#include <sys/devfs_rules.h>

MALLOC_DECLARE(M_DEVFS);

static d_open_t      devfs_dev_open;
static d_close_t     devfs_dev_close;
static d_ioctl_t     devfs_dev_ioctl;

static struct devfs_rule *devfs_rule_alloc(struct devfs_rule_ioctl *);
static void devfs_rule_free(struct devfs_rule *);
static int devfs_rule_insert(struct devfs_rule_ioctl *);
static void devfs_rule_remove(struct devfs_rule *);
static int devfs_rule_clear(struct devfs_rule_ioctl *);
static void devfs_rule_create_link(struct devfs_node *, struct devfs_rule *);
static int devfs_rule_checkname(struct devfs_rule *, struct devfs_node *);

static struct objcache	*devfs_rule_cache;
static struct lock 		devfs_rule_lock;

static struct objcache_malloc_args devfs_rule_malloc_args = {
	sizeof(struct devfs_rule), M_DEVFS };

static cdev_t devfs_dev;
static struct devfs_rule_head devfs_rule_list =
		TAILQ_HEAD_INITIALIZER(devfs_rule_list);

static struct dev_ops devfs_dev_ops = {
	{ "devfs", 0, 0 },
	.d_open = devfs_dev_open,
	.d_close = devfs_dev_close,
	.d_ioctl = devfs_dev_ioctl
};


static struct devfs_rule *
devfs_rule_alloc(struct devfs_rule_ioctl *templ)
{
	struct devfs_rule *rule;
	size_t len;

	rule = objcache_get(devfs_rule_cache, M_WAITOK);
	memset(rule, 0, sizeof(struct devfs_rule));

	if (templ->mntpoint == NULL)
		goto error_out;
		/* NOTREACHED */

	len = strlen(templ->mntpoint);
	if (len == 0)
		goto error_out;
		/* NOTREACHED */

	rule->mntpoint = kstrdup(templ->mntpoint, M_DEVFS);
	rule->mntpointlen = len;

	if (templ->rule_type & DEVFS_RULE_NAME) {
		if (templ->name == NULL)
			goto error_out;
			/* NOTREACHED */

		len = strlen(templ->name);
		if (len == 0)
			goto error_out;
			/* NOTREACHED */

		rule->name = kstrdup(templ->name, M_DEVFS);
		rule->namlen = len;
	}

	if (templ->rule_cmd & DEVFS_RULE_LINK) {
		if (templ->linkname == NULL)
			goto error_out;
			/* NOTREACHED */

		len = strlen(templ->linkname);
		if (len == 0)
			goto error_out;
			/* NOTREACHED */

		rule->linkname = kstrdup(templ->linkname, M_DEVFS);
		rule->linknamlen = len;
	}

	rule->rule_type = templ->rule_type;
	rule->rule_cmd = templ->rule_cmd;
	rule->dev_type = templ->dev_type;
	rule->mode = templ->mode;
	rule->uid = templ->uid;
	rule->gid = templ->gid;

	return rule;

error_out:
	devfs_rule_free(rule);
	return NULL;
}


static void
devfs_rule_free(struct devfs_rule *rule)
{
	if (rule->mntpoint != NULL) {
		kfree(rule->mntpoint, M_DEVFS);
	}

	if (rule->name != NULL) {
		kfree(rule->name, M_DEVFS);
	}

	if (rule->linkname != NULL) {
		kfree(rule->linkname, M_DEVFS);
	}
	objcache_put(devfs_rule_cache, rule);
}


static int
devfs_rule_insert(struct devfs_rule_ioctl *templ)
{
	struct devfs_rule *rule;

	rule = devfs_rule_alloc(templ);
	if (rule == NULL)
		return EINVAL;

	lockmgr(&devfs_rule_lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
	lockmgr(&devfs_rule_lock, LK_RELEASE);

	return 0;
}


static void
devfs_rule_remove(struct devfs_rule *rule)
{
	TAILQ_REMOVE(&devfs_rule_list, rule, link);
	devfs_rule_free(rule);
}


static int
devfs_rule_clear(struct devfs_rule_ioctl *templ)
{
	struct devfs_rule *rule1, *rule2;
	size_t mntpointlen;

	if (templ->mntpoint == NULL)
		return EINVAL;

	mntpointlen = strlen(templ->mntpoint);
	if (mntpointlen == 0)
		return EINVAL;

	lockmgr(&devfs_rule_lock, LK_EXCLUSIVE);
	TAILQ_FOREACH_MUTABLE(rule1, &devfs_rule_list, link, rule2) {
		if ((templ->mntpoint[0] == '*') ||
			( (mntpointlen == rule1->mntpointlen) &&
			  (!memcmp(templ->mntpoint, rule1->mntpoint, mntpointlen)) )) {
			devfs_rule_remove(rule1);
		}
	}
	lockmgr(&devfs_rule_lock, LK_RELEASE);

	return 0;
}


void *
devfs_rule_reset_node(struct devfs_node *node, void *unused)
{
	/*
	 * Don't blindly unhide all devices, some, like unix98 pty masters,
	 * haven't been hidden by a rule.
	 */
	if (node->flags & DEVFS_RULE_HIDDEN)
		node->flags &= ~(DEVFS_HIDDEN | DEVFS_RULE_HIDDEN);

	if ((node->node_type == Nlink) && (node->flags & DEVFS_RULE_CREATED)) {
		KKASSERT(node->link_target);
		node->flags &= ~DEVFS_RULE_CREATED;
		--node->link_target->nlinks;
		devfs_gc(node);
	} else if ((node->node_type == Ndev) && (node->d_dev)) {
		node->uid = node->d_dev->si_uid;
		node->gid = node->d_dev->si_gid;
		node->mode = node->d_dev->si_perms;
	}

	return NULL;
}

static void
devfs_rule_create_link(struct devfs_node *node, struct devfs_rule *rule)
{
	size_t len = 0;
	char *path = NULL;
	char *name, name_buf[PATH_MAX], buf[PATH_MAX];

	if (rule->name[rule->namlen-1] == '*') {
		devfs_resolve_name_path(rule->name, name_buf, &path, &name);
		len = strlen(name);
		--len;
		ksnprintf(buf, sizeof(buf), "%s%s",
		    rule->linkname, node->d_dir.d_name+len);
		devfs_alias_create(buf, node, 1);
	} else {
		devfs_alias_create(rule->linkname, node, 1);
	}
}

void *
devfs_rule_check_apply(struct devfs_node *node, void *unused)
{
	struct devfs_rule *rule;
	struct mount *mp = node->mp;
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the devfs lock */
	if ((lockstatus(&devfs_rule_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&devfs_rule_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	TAILQ_FOREACH(rule, &devfs_rule_list, link) {
		/*
		 * Skip this rule if it is only intended for jailed mount points
		 * and the current mount point isn't jailed
		 */
		if ((rule->rule_type & DEVFS_RULE_JAIL) &&
			(!(DEVFS_MNTDATA(mp)->jailed)) )
			continue;

		/*
		 * Skip this rule if it is not intended for jailed mount points
		 * and the current mount point is jailed.
		 */
		if (!(rule->rule_type & DEVFS_RULE_JAIL) &&
			(DEVFS_MNTDATA(mp)->jailed))
		    continue;

		/*
		 * Skip this rule if the mount point specified in the rule doesn't
		 * match the mount point of the node
		 */
		if ((rule->mntpoint[0] != '*') &&
			(strcmp(rule->mntpoint, mp->mnt_stat.f_mntonname)))
			continue;

		/*
		 * Skip this rule if this is a by-type rule and the device flags
		 * don't match the specified device type in the rule
		 */
		if ((rule->rule_type & DEVFS_RULE_TYPE) &&
			( (rule->dev_type == 0) || (!dev_is_good(node->d_dev)) ||
			  (!(dev_dflags(node->d_dev) & rule->dev_type))) )
			continue;

		/*
		 * Skip this rule if this is a by-name rule and the node name
		 * doesn't match the wildcard string in the rule
		 */
		if ((rule->rule_type & DEVFS_RULE_NAME) &&
			(!devfs_rule_checkname(rule, node)) )
			continue;

		if (rule->rule_cmd & DEVFS_RULE_HIDE) {
			/*
			 * If we should hide the device, we just apply the relevant
			 * hide flag to the node and let devfs do the rest in the
			 * vnops
			 */
			if ((node->d_dir.d_namlen == 5) &&
				(!memcmp(node->d_dir.d_name, "devfs", 5))) {
				/*
				 * Magically avoid /dev/devfs from being hidden, so that one
				 * can still use the rule system even after a "* hide".
				 */
				 continue;
			}
			node->flags |= (DEVFS_HIDDEN | DEVFS_RULE_HIDDEN);
		} else if (rule->rule_cmd & DEVFS_RULE_SHOW) {
			/*
			 * Show rule just means that the node should not be hidden, so
			 * what we do is clear the hide flag from the node.
			 */
			node->flags &= ~DEVFS_HIDDEN;
		} else if (rule->rule_cmd & DEVFS_RULE_LINK) {
			/*
			 * This is a LINK rule, so we tell devfs to create
			 * a link with the correct name to this node.
			 */
			devfs_rule_create_link(node, rule);
#if 0
			devfs_alias_create(rule->linkname, node, 1);
#endif
		} else if (rule->rule_cmd & DEVFS_RULE_PERM) {
			/*
			 * This is a normal ownership/permission rule. We
			 * just apply the permissions and ownership and
			 * we are done.
			 */
			node->mode = rule->mode;
			node->uid = rule->uid;
			node->gid = rule->gid;
		}
	}

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&devfs_rule_lock, LK_RELEASE);

	return NULL;
}


static int
devfs_rule_checkname(struct devfs_rule *rule, struct devfs_node *node)
{
	struct devfs_node *parent = DEVFS_MNTDATA(node->mp)->root_node;
	char *path = NULL;
	char *name, name_buf[PATH_MAX];
	int no_match = 0;

	devfs_resolve_name_path(rule->name, name_buf, &path, &name);
	parent = devfs_resolve_or_create_path(parent, path, 0);

	if (parent == NULL)
		return 0; /* no match */

	/* Check if node is a child of the parent we found */
	if (node->parent != parent)
		return 0; /* no match */

#if 0
	if (rule->rule_type & DEVFS_RULE_LINK)
		no_match = memcmp(name, node->d_dir.d_name, strlen(name));
	else
#endif
	no_match = devfs_WildCaseCmp(name, node->d_dir.d_name);

	return !no_match;
}


static int
devfs_dev_open(struct dev_open_args *ap)
{
	/*
	 * Only allow read-write access.
	 */
	if (((ap->a_oflags & FWRITE) == 0) || ((ap->a_oflags & FREAD) == 0))
		return(EPERM);

	/*
	 * We don't allow nonblocking access.
	 */
	if ((ap->a_oflags & O_NONBLOCK) != 0) {
		devfs_debug(DEVFS_DEBUG_SHOW, "devfs_dev: can't do nonblocking access\n");
		return(ENODEV);
	}

	return 0;
}


static int
devfs_dev_close(struct dev_close_args *ap)
{
	return 0;
}


static int
devfs_dev_ioctl(struct dev_ioctl_args *ap)
{
	int error;
	struct devfs_rule_ioctl *rule;

	error = 0;
	rule = (struct devfs_rule_ioctl *)ap->a_data;

	switch(ap->a_cmd) {
	case DEVFS_RULE_ADD:
		error = devfs_rule_insert(rule);
		break;

	case DEVFS_RULE_APPLY:
		if (rule->mntpoint == NULL)
			error = EINVAL;
		else
			devfs_apply_rules(rule->mntpoint);
		break;

	case DEVFS_RULE_CLEAR:
		error = devfs_rule_clear(rule);
		break;

	case DEVFS_RULE_RESET:
		if (rule->mntpoint == NULL)
			error = EINVAL;
		else
			devfs_reset_rules(rule->mntpoint);
		break;

	default:
		error = ENOTTY; /* Inappropriate ioctl for device */
		break;
	}

	return(error);
}


static void
devfs_dev_init(void *unused)
{
	lockinit(&devfs_rule_lock, "devfs_rule lock", 0, 0);

    devfs_rule_cache = objcache_create("devfs-rule-cache", 0, 0,
			NULL, NULL, NULL,
			objcache_malloc_alloc,
			objcache_malloc_free,
			&devfs_rule_malloc_args );

    devfs_dev = make_dev(&devfs_dev_ops,
            0,
            UID_ROOT,
            GID_WHEEL,
            0600,
            "devfs");
}


static void
devfs_dev_uninit(void *unused)
{
	/* XXX: destroy all rules first */
    destroy_dev(devfs_dev);
	objcache_destroy(devfs_rule_cache);
}


SYSINIT(devfsdev,SI_SUB_DRIVERS,SI_ORDER_FIRST,devfs_dev_init,NULL);
SYSUNINIT(devfsdev, SI_SUB_DRIVERS,SI_ORDER_FIRST,devfs_dev_uninit, NULL);

