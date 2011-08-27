#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <sys/fcntl.h>
#include <machine/varargs.h>

/*
 * A simple anticipatory scheduler
 */
struct as_disk_ctx{
	struct dsched_disk_ctx head;
	TAILQ_HEAD(, bio) as_queue_rd;
	TAILQ_HEAD(, bio) as_queue_wr;
	/*
	 * TODO: lockmgr may be too heavy here,
	 * use spinlock instead!
	 */
	struct lock as_queue_rd_lock;
	struct lock as_queue_wr_lock;
	int queue_rd_size;
	int queue_wr_size;

	struct callout as_callout;

	int as_blockall;
	pid_t as_blockon;
};

struct dsched_as_stats{
	int32_t unused;
}as_stats;

static dsched_prepare_t		as_prepare;
static dsched_teardown_t	as_teardown;
static dsched_cancel_t		as_cancel;
static dsched_queue_t		as_queue;
static dsched_polling_func_t	as_dequeue;

static struct dsched_policy dsched_as_policy = {
	.name ="as",
/*
 * field need_request_polling
 * is removed from struct dsched_policy
 */
	//.need_request_polling = 1,
	.prepare = as_prepare,
	.teardown = as_teardown,
	.cancel_all = as_cancel,
	.bio_queue = as_queue,
	.polling_func = as_dequeue
};

static int dsched_as_version_maj = 1;
static int dsched_as_version_min = 0;

static int
as_prepare(struct dsched_disk_ctx *diskctx)
{
	struct as_disk_ctx *as_diskctx = (struct as_disk_ctx *)diskctx;
	TAILQ_INIT(&as_diskctx->as_queue_wr);
	as_diskctx->queue_wr_size = 0;
	TAILQ_INIT(&as_diskctx->as_queue_rd);
	as_diskctx->queue_rd_size = 0;
	lockinit(&as_diskctx->as_queue_rd_lock, "as_queue_rd", 0, LK_CANRECURSE);
	lockinit(&as_diskctx->as_queue_wr_lock, "as_queue_wr", 0, LK_CANRECURSE);
	callout_init(&as_diskctx->as_callout);
	as_diskctx->as_blockall = 0;
	as_diskctx->as_blockon = NO_PID;
	return 0;
}

static void
as_teardown(struct dsched_disk_ctx *diskctx)
{


}

static void
as_cancel(struct dsched_disk_ctx *diskctx)
{
	struct as_disk_ctx *as_diskctx = (struct as_disk_ctx *)diskctx;
	struct bio *bio, *bio2;
	struct dsched_thread_io *tdio;
	DSCHED_DISK_CTX_LOCK(&as_diskctx->head);
	lockmgr(&as_diskctx->as_queue_rd_lock, LK_EXCLUSIVE);
	TAILQ_FOREACH_MUTABLE(bio, &as_diskctx->as_queue_rd, link, bio2){
		TAILQ_REMOVE(&as_diskctx->as_queue_rd, bio, link);
		tdio = dsched_get_bio_tdio(bio);
		dsched_cancel_bio(bio);
		dsched_thread_io_unref(tdio);
	}
	lockmgr(&as_diskctx->as_queue_rd_lock, LK_RELEASE);

	lockmgr(&as_diskctx->as_queue_wr_lock, LK_EXCLUSIVE);
	TAILQ_FOREACH_MUTABLE(bio, &as_diskctx->as_queue_wr, link, bio2){
		TAILQ_REMOVE(&as_diskctx->as_queue_wr, bio, link);
		tdio = dsched_get_bio_tdio(bio);
		dsched_cancel_bio(bio);
		dsched_thread_io_unref(tdio);
	}
	lockmgr(&as_diskctx->as_queue_wr_lock, LK_RELEASE);

	DSCHED_DISK_CTX_UNLOCK(&as_diskctx->head);
}

static void
as_timeout(void *p)
{
	pid_t last_blockon;
	struct as_disk_ctx *as_diskctx = (struct as_disk_ctx *)p;
	DSCHED_DISK_CTX_LOCK(&as_diskctx->head);
	as_diskctx->as_blockall = 0;
	last_blockon = as_diskctx->as_blockon;
	as_diskctx->as_blockon = NO_PID;
	DSCHED_DISK_CTX_UNLOCK(&as_diskctx->head);
	//dsched_debug(0, "dsched: as, timeout %d\n", last_blockon);
	as_dequeue((struct dsched_disk_ctx *)as_diskctx);
}

static int
as_queue(struct dsched_disk_ctx *diskctx, struct dsched_thread_io *tdio,
		struct  bio *bio)
{
	struct as_disk_ctx *as_diskctx = (struct as_disk_ctx *)diskctx;
	//if (tdio->p && (uint32_t)tdio->p != ~0)
	//	dsched_debug(0, "dsched: user process bio from %d\n", tdio->p->p_pid);
	/*save tdio for each bio*/
	dsched_set_bio_priv(bio, tdio);
	dsched_set_bio_tdio(bio, tdio);
	/* will be unreferenced in bio_done function */
	dsched_thread_io_ref(tdio);
	DSCHED_DISK_CTX_LOCK(&as_diskctx->head);
	/* blocking for as,
	 * if current bio is from as_blockon, insert it at head
	 */
	if (bio->bio_buf->b_cmd == BUF_CMD_READ){
		lockmgr(&as_diskctx->as_queue_rd_lock, LK_EXCLUSIVE);
		if (as_diskctx->as_blockall && tdio->p && as_diskctx->as_blockon == tdio->p->p_pid)
			TAILQ_INSERT_HEAD(&as_diskctx->as_queue_rd, bio, link);
		else
			TAILQ_INSERT_TAIL(&as_diskctx->as_queue_rd, bio, link);
		atomic_add_int(&as_diskctx->queue_rd_size, 1);
		lockmgr(&as_diskctx->as_queue_rd_lock, LK_RELEASE);
	} else {
		lockmgr(&as_diskctx->as_queue_wr_lock, LK_EXCLUSIVE);
		TAILQ_INSERT_TAIL(&as_diskctx->as_queue_wr, bio, link);
		atomic_add_int(&as_diskctx->queue_wr_size, 1);
		lockmgr(&as_diskctx->as_queue_wr_lock, LK_RELEASE);
	}
	DSCHED_DISK_CTX_UNLOCK(&as_diskctx->head);
	as_dequeue(diskctx);
	return 0;

}
static void
as_dequeue(struct dsched_disk_ctx *diskctx)
{
	int free_slots = 0;
	struct as_disk_ctx *as_diskctx = (struct as_disk_ctx *)diskctx;
	struct bio *bio;
	struct dsched_thread_io *tdio;
	/*Lock the diskctx for the whole dispatching process,
	 * to ensure atomic change to current_tab_queue_depth*/

	DSCHED_DISK_CTX_LOCK(&as_diskctx->head);
	/* if blocking all dispatching for anticipatory scheduling
	 * return directly
	 */
	if (as_diskctx->as_blockall){
	//	dsched_debug(0, "dsched: as, dequeue blocked! %d\n", as_diskctx->as_blockon);
		goto rtn;
	}
	free_slots = as_diskctx->head.max_tag_queue_depth - as_diskctx->head.current_tag_queue_depth;
	KKASSERT(free_slots>=0 && free_slots <=64);
	lockmgr(&as_diskctx->as_queue_rd_lock, LK_EXCLUSIVE);
	while (free_slots > 0){
		if (TAILQ_EMPTY(&as_diskctx->as_queue_rd))
			break;
		bio = TAILQ_FIRST(&as_diskctx->as_queue_rd);
		tdio = dsched_get_bio_priv(bio);
		//kernel thread
		if (!tdio->p || (uint32_t)(tdio->p) == ~0){
			TAILQ_REMOVE(&as_diskctx->as_queue_rd, bio, link);
			dsched_strategy_request_polling(as_diskctx->head.dp, bio, diskctx);
		} else {
			//user process, continue
//			dsched_debug(0, "dsched: as, user process bio\n");
			if (as_diskctx->as_blockon == NO_PID || as_diskctx->as_blockon == tdio->p->p_pid){
				as_diskctx->as_blockon = tdio->p->p_pid;
				TAILQ_REMOVE(&as_diskctx->as_queue_rd, bio, link);
				dsched_strategy_request_polling(as_diskctx->head.dp, bio, diskctx);
			} else {
				//user process, before switching, as!
				as_diskctx->as_blockall = 1;
//				dsched_debug(0, "dsched: as, block on %d\n", as_diskctx->as_blockon);
				callout_reset(&as_diskctx->as_callout, 10, as_timeout, as_diskctx);
				break;
			}

		}
		free_slots --;
	}
	lockmgr(&as_diskctx->as_queue_rd_lock, LK_RELEASE);
	lockmgr(&as_diskctx->as_queue_wr_lock, LK_EXCLUSIVE);
	while (free_slots > 0){
		if (TAILQ_EMPTY(&as_diskctx->as_queue_wr))
			break;
		bio = TAILQ_FIRST(&as_diskctx->as_queue_wr);
		TAILQ_REMOVE(&as_diskctx->as_queue_wr, bio, link);
		dsched_strategy_request_polling(as_diskctx->head.dp, bio, diskctx);
		free_slots --;
	}
	lockmgr(&as_diskctx->as_queue_wr_lock, LK_RELEASE);

rtn:
	DSCHED_DISK_CTX_UNLOCK(&as_diskctx->head);
}

static int
do_asstats(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, &as_stats, sizeof(struct dsched_as_stats), req));
}

static int
as_mod_handler(module_t mod, int type, void *unused)
{
	static struct sysctl_ctx_list sysctl_ctx;
	static struct sysctl_oid *oid;
	static char version[16];
	int error;

	ksnprintf(version, sizeof(version), "%d.%d",
	    dsched_as_version_maj, dsched_as_version_min);

	switch (type) {
	case MOD_LOAD:
		bzero(&as_stats, sizeof(struct dsched_as_stats));
		if ((error = dsched_register(&dsched_as_policy)))
			return (error);

		sysctl_ctx_init(&sysctl_ctx);
		oid = SYSCTL_ADD_NODE(&sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_dsched),
		    OID_AUTO,
		    "as",
		    CTLFLAG_RD, 0, "");

		SYSCTL_ADD_PROC(&sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "stats", CTLTYPE_OPAQUE|CTLFLAG_RD,
		    0, 0, do_asstats, "S,dsched_as_stats", "as statistics");

		SYSCTL_ADD_STRING(&sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "version", CTLFLAG_RD, version, 0, "as version");

		kprintf("AS scheduler policy version %d.%d loaded\n",
		    dsched_as_version_maj, dsched_as_version_min);
		break;

	case MOD_UNLOAD:
		if ((error = dsched_unregister(&dsched_as_policy)))
			return (error);
		sysctl_ctx_free(&sysctl_ctx);
		kprintf("AS scheduler policy unloaded\n");
		break;

	default:
		break;
	}

	return 0;

}
DSCHED_POLICY_MODULE(dsched_as, as_mod_handler);
