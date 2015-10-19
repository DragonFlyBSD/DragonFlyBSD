/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Tomohiro Kusumi <kusumi.tomohiro@gmail.com>
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

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mutex2.h>
#include <sys/objcache.h>
#include <sys/callout.h>

#include <dev/disk/dm/dm.h>

MALLOC_DEFINE(M_DMDELAY, "dm_delay", "Device Mapper Target Delay");

struct dm_delay_buf {
	TAILQ_ENTRY(dm_delay_buf) entry;
	struct buf *bp;
	int expire;
};
TAILQ_HEAD(dm_delay_buf_list, dm_delay_buf);

struct dm_delay_info {
	dm_pdev_t *pdev;
	uint64_t offset;
	int delay;
	int count;
	int enabled;
	struct dm_delay_buf_list buf_list;
	struct callout cal;
	struct mtx buf_mtx;
	struct mtx cal_mtx;
	struct lwkt_token token;
	thread_t td;
};

typedef struct target_delay_config {
	struct dm_delay_info read;
	struct dm_delay_info write;
	int argc;  /* either 3 or 6 */
} dm_target_delay_config_t;

static int _init(struct dm_delay_info *di, char **argv, int id);
static int _status(struct dm_delay_info *di, char *p);
static void _strategy(struct dm_delay_info *di, struct buf *bp);
static void _submit(struct dm_delay_info *di, struct buf *bp);
static void _submit_queue(struct dm_delay_info *di, int submit_all);
static void _destroy(struct dm_delay_info *di);
static uint64_t _makeudev(struct dm_delay_info *di);
static void _timeout(void *arg);
static void _thread(void *arg);
static __inline void _debug(struct dm_delay_info *di, const char *msg);

static struct objcache *obj_cache = NULL;
static struct objcache_malloc_args obj_args = {
	sizeof(struct dm_delay_buf), M_DMDELAY,
};

static int
dm_target_delay_init(dm_dev_t *dmv, void **target_config, char *params)
{
	dm_target_delay_config_t *tdc;
	int ret, argc;
	char **ap, *argv[7];

	if (params == NULL)
		return EINVAL;

	/* Parse params */
	for (ap = argv; ap < &argv[6] &&
	    (*ap = strsep(&params, " \t")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}

	argc = ap - argv;
	aprint_debug("Delay target init: argc=%d\n", argc);
	if (argc != 3 && argc != 6) {
		kprintf("Delay target takes 3 or 6 args\n");
		return EINVAL;
	}

	tdc = kmalloc(sizeof(*tdc), M_DMDELAY, M_WAITOK | M_ZERO);
	tdc->argc = argc;

	ap = argv;
	ret = _init(&tdc->read, ap, 0);
	if (ret) {
		kfree(tdc, M_DMDELAY);
		return ret;
	}

	if (argc == 6)
		ap += 3;

	ret = _init(&tdc->write, ap, 1);
	if (ret) {
		dm_pdev_decr(tdc->read.pdev);
		kfree(tdc, M_DMDELAY);
		return ret;
	}

	*target_config = tdc;
	dmv->dev_type = DM_DELAY_DEV;

	return 0;
}

static int
_init(struct dm_delay_info *di, char **argv, int id)
{
	dm_pdev_t *dmp;
	int tmp;

	if (argv[0] == NULL)
		return EINVAL;
	if ((dmp = dm_pdev_insert(argv[0])) == NULL)
		return ENOENT;

	di->pdev = dmp;
	di->offset = atoi64(argv[1]);
	tmp = atoi64(argv[2]);
	di->delay = tmp * hz / 1000;
	di->count = 0;

	TAILQ_INIT(&di->buf_list);
	callout_init(&di->cal);
	mtx_init(&di->buf_mtx, "dmdlbuf");
	mtx_init(&di->cal_mtx, "dmdlcal");
	lwkt_token_init(&di->token, "dmdlthr");

	di->enabled = 1;
	lwkt_create(_thread, di, &di->td, NULL, 0, -1, "dmdl%d", id);

	_debug(di, "init");
	return 0;
}

static char *
dm_target_delay_info(void *target_config)
{
	dm_target_delay_config_t *tdc;
	char *params;

	tdc = target_config;
	KKASSERT(tdc != NULL);

	params = kmalloc(DM_MAX_PARAMS_SIZE, M_DM, M_WAITOK);
	ksnprintf(params, DM_MAX_PARAMS_SIZE,
		"%d %d", tdc->read.count, tdc->write.count);

	return params;
}

static char *
dm_target_delay_status(void *target_config)
{
	dm_target_delay_config_t *tdc;
	char *params, *p;

	tdc = target_config;
	KKASSERT(tdc != NULL);

	params = kmalloc(DM_MAX_PARAMS_SIZE, M_DM, M_WAITOK);
	p = params;
	p += _status(&tdc->read, p);
	if (tdc->argc == 6) {
		p += ksnprintf(p, DM_MAX_PARAMS_SIZE, " ");
		_status(&tdc->write, p);
	}

	return params;
}

static int _status(struct dm_delay_info *di, char *p)
{
	int ret = 0;

	ret += ksnprintf(p, DM_MAX_PARAMS_SIZE,
		"%s %" PRIu64 " %d",
		di->pdev->name, di->offset, di->delay);
	return ret;
}

static int
dm_target_delay_strategy(dm_table_entry_t *table_en, struct buf *bp)
{
	dm_target_delay_config_t *tdc;
	struct dm_delay_info *di;

	tdc = table_en->target_config;
	KKASSERT(tdc != NULL);

	switch (bp->b_cmd) {
	case BUF_CMD_READ:
		di = &tdc->read;
		break;
	case BUF_CMD_WRITE:
	case BUF_CMD_FLUSH:
		di = &tdc->write;
		break;
	default:
		di = NULL;
		break;
	}

	if (di) {
		if (di->delay) {
			_strategy(di, bp);
		} else {
			_submit(di, bp);
		}
	} else {
		/* XXX */
		struct vnode *vnode = tdc->write.pdev->pdev_vnode;
		vn_strategy(vnode, &bp->b_bio1);
	}
	return 0;
}

static void
_strategy(struct dm_delay_info *di, struct buf *bp)
{
	struct dm_delay_buf *dp;

	dp = objcache_get(obj_cache, M_WAITOK);
	dp->bp = bp;
	dp->expire = ticks + di->delay;

	mtx_lock(&di->buf_mtx);
	di->count++;
	TAILQ_INSERT_TAIL(&di->buf_list, dp, entry);
	mtx_unlock(&di->buf_mtx);

	mtx_lock(&di->cal_mtx);
	if (!callout_pending(&di->cal))
		callout_reset(&di->cal, di->delay, _timeout, di);
	mtx_unlock(&di->cal_mtx);
}

static void
_submit(struct dm_delay_info *di, struct buf *bp)
{
	_debug(di, "submit");

	bp->b_bio1.bio_offset += di->offset * DEV_BSIZE;
	vn_strategy(di->pdev->pdev_vnode, &bp->b_bio1);
}

static void
_submit_queue(struct dm_delay_info *di, int submit_all)
{
	struct dm_delay_buf *dp;
	struct dm_delay_buf_list tmp_list;
	int next = -1;
	int reset = 0;

	_debug(di, "submitq");
	TAILQ_INIT(&tmp_list);

	mtx_lock(&di->buf_mtx);
	while ((dp = TAILQ_FIRST(&di->buf_list)) != NULL) {
		if (submit_all || ticks > dp->expire) {
			TAILQ_REMOVE(&di->buf_list, dp, entry);
			TAILQ_INSERT_TAIL(&tmp_list, dp, entry);
			di->count--;
			continue;
		}
		if (reset == 0) {
			reset = 1;
			next = dp->expire;
		} else {
			next = min(next, dp->expire);
		}
	}
	mtx_unlock(&di->buf_mtx);

	if (reset) {
		mtx_lock(&di->cal_mtx);
		callout_reset(&di->cal, next - ticks, _timeout, di);
		mtx_unlock(&di->cal_mtx);
	}

	while ((dp = TAILQ_FIRST(&tmp_list)) != NULL) {
		TAILQ_REMOVE(&tmp_list, dp, entry);
		_submit(di, dp->bp);
		objcache_put(obj_cache, dp);
	}
}

static int
dm_target_delay_destroy(dm_table_entry_t *table_en)
{
	dm_target_delay_config_t *tdc;

	tdc = table_en->target_config;
	if (tdc == NULL)
		return 0;

	_destroy(&tdc->read);
	_destroy(&tdc->write);

	kfree(tdc, M_DMDELAY);
	table_en->target_config = NULL;

	return 0;
}

static void
_destroy(struct dm_delay_info *di)
{
	_debug(di, "destroy");

	lwkt_gettoken(&di->token);
	di->enabled = 0;

	mtx_lock(&di->cal_mtx);
	if (callout_pending(&di->cal))
		callout_stop_sync(&di->cal);
	mtx_unlock(&di->cal_mtx);

	_submit_queue(di, 1);
	wakeup(di);
	tsleep(&di->enabled, 0, "dmdldestroy", 0);
	lwkt_reltoken(&di->token);

	mtx_uninit(&di->cal_mtx);
	mtx_uninit(&di->buf_mtx);

	dm_pdev_decr(di->pdev);
}

static int
dm_target_delay_deps(dm_table_entry_t *table_en, prop_array_t prop_array)
{
	dm_target_delay_config_t *tdc;
	uint64_t u1, u2;

	tdc = table_en->target_config;
	if (tdc == NULL)
		return ENOENT;

	u1 = _makeudev(&tdc->read);
	u2 = _makeudev(&tdc->write);

	prop_array_add_uint64(prop_array, u1);
	if (u1 != u2) {
		prop_array_add_uint64(prop_array, u2);
	}
	return 0;
}

static uint64_t
_makeudev(struct dm_delay_info *di)
{
	struct vattr va;
	uint64_t ret;
	int error;

	_debug(di, "makeudev");

	error = VOP_GETATTR(di->pdev->pdev_vnode, &va);
	KKASSERT(error == 0);

	ret = makeudev(va.va_rmajor, va.va_rminor);
	return ret;
}

static int
dm_target_delay_upcall(dm_table_entry_t *table_en, struct buf *bp)
{
	return 0;
}

static void
_timeout(void *arg)
{
	struct dm_delay_info *di = arg;

	_debug(di, "timeout");
	wakeup(di);
}

static void
_thread(void *arg)
{
	struct dm_delay_info *di = arg;

	_debug(di, "thread init");
	lwkt_gettoken(&di->token);

	while (di->enabled) {
		tsleep(di, 0, "dmdlthread", 0);
		_submit_queue(di, 0);
	}

	di->td = NULL;
	wakeup(&di->enabled);

	_debug(di, "thread exit");
	lwkt_reltoken(&di->token);
	lwkt_exit();
}

static __inline
void
_debug(struct dm_delay_info *di, const char *msg)
{
	aprint_debug("%-8s: %d pdev=%s offset=%ju delay=%d count=%d\n",
		msg, di->enabled, di->pdev->name,
		(uintmax_t)di->offset, di->delay, di->count);
}

static void
_objcache_create(void)
{
	if (obj_cache == NULL) {
		obj_cache = objcache_create("dmdlobj", 0, 0, NULL, NULL, NULL,
			objcache_malloc_alloc,
			objcache_malloc_free,
			&obj_args);
	}
	KKASSERT(obj_cache);
}

static void
_objcache_destroy(void)
{
	if (obj_cache) {
		objcache_destroy(obj_cache);
		obj_cache = NULL;
	}
}

static int
dmtd_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch(type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("delay")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("delay");
		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "delay", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_delay_init;
		dmt->info = &dm_target_delay_info;
		dmt->status = &dm_target_delay_status;
		dmt->strategy = &dm_target_delay_strategy;
		dmt->destroy = &dm_target_delay_destroy;
		dmt->deps = &dm_target_delay_deps;
		dmt->upcall = &dm_target_delay_upcall;
		dmt->dump = NULL;

		_objcache_create();
		err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_delay: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_rem("delay");
		if (err == 0)
			kprintf("dm_target_delay: unloaded\n");
		_objcache_destroy();
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_delay, dmtd_mod_handler);
