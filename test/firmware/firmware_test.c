/* Host-side API shims; the production functions are inserted below. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#define MAXPATHLEN 1024
#define M_TEMP 0
#define M_WAITOK 1
#define M_NULLOK 2
#define MALLOC_DEFINE(name, shortname, description) const int name = 1
#define SYSCTL_STRING(...)
#define SYSCTL_ULONG(...)
#define TUNABLE_STR(...)
#undef __DECONST
#define __DECONST(type, value) ((type)(uintptr_t)(value))
#define KKASSERT assert
#define kprintf(...) ((void)0)
#define ksnprintf snprintf
#define LK_EXCLUSIVE 1
#define LK_RELEASE 2
#define FIRMWARE_UNLOAD 1
#define SYSCAP_NOKLD 0
#define UIO_SYSSPACE 0
#define UIO_READ 0
#define NLC_FOLLOW 1
#define NLC_LOCKVP 2
#define FREAD 1
#define IO_NODELOCKED 1
#define VREG 1
#define MODINFO_TYPE 1
#define MODINFO_NAME 2
#define MODINFO_ADDR 3
#define MODINFO_SIZE 4

struct firmware {
	const char *name;
	const uint8_t *data;
	size_t datasize;
	unsigned version;
};
typedef void *linker_file_t;
struct lock {
	int held;
};
struct task {
	void (*function)(void *, int);
	void *argument;
};
struct taskqueue {
	int unused;
};
struct vnode {
	int v_type;
	const char *path;
	const char *contents;
	size_t size;
};
struct vattr {
	off_t va_size;
};
struct nlookupdata {
	struct vnode *nl_open_vp;
};
static int root_credential;
static struct {
	void *p_ucred;
} proc0 = {&root_credential};
static int cold, securelevel, bootverbose;
static struct task *pending;
static size_t allocations;
static int allocation_count, fail_allocation;
static int module_loads, module_releases, read_error, short_read;
static struct vnode files[] = {
    {VREG, "/first/load.bin", "LOAD", 4},
    {VREG, "/first/unload.bin", "STOP", 4},
    {VREG, "/second/load.bin", "NEXT", 4},
    {VREG, "/first/empty.bin", "", 0},
    {VREG, "/first/large.bin", "LARGE", 5},
    {0, "/first/directory", "", 0},
};
static char preload_name[] = "/boot/firmware/chip/preload.bin";
static unsigned char preload_bytes[] = "PRELOAD";
static void *preload_address = preload_bytes;
static size_t preload_size = sizeof(preload_bytes);
static bool preload_enabled;

static void *kmalloc(size_t size, int type, int flags)
{
	if (++allocation_count == fail_allocation)
		return NULL;
	void *p = malloc(size);
	assert(p != NULL);
	++allocations;
	return p;
}
static void kfree(void *p, int type)
{
	assert(p != NULL && allocations > 0);
	--allocations;
	free(p);
}
static void lockmgr(struct lock *lock, int operation)
{
	if (operation == LK_EXCLUSIVE) {
		assert(!lock->held);
		lock->held = 1;
	} else {
		assert(lock->held);
		lock->held = 0;
	}
}
static int caps_priv_check_self(int capability) { return 0; }
#define TASK_INIT(task, priority, callback, arg)                               \
	do {                                                                   \
		(task)->function = (callback);                                 \
		(task)->argument = (arg);                                      \
	} while (0)
static void taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	assert(pending == NULL || pending == task);
	pending = task;
}
static void run_task(void)
{
	struct task *task = pending;
	pending = NULL;
	assert(task != NULL);
	task->function(task->argument, 1);
}
static void lksleep(const void *id, struct lock *lock, int flags,
		    const char *message, int timeout)
{
	lockmgr(lock, LK_RELEASE);
	run_task();
	lockmgr(lock, LK_EXCLUSIVE);
}
static void wakeup_one(const void *id) {}
static caddr_t preload_search_next_name(caddr_t previous)
{
	return preload_enabled && previous == NULL ? (caddr_t)preload_name
						   : NULL;
}
static caddr_t preload_search_info(caddr_t module, int field)
{
	switch (field) {
	case MODINFO_TYPE:
		return "firmware";
	case MODINFO_NAME:
		return preload_name;
	case MODINFO_ADDR:
		return (caddr_t)&preload_address;
	case MODINFO_SIZE:
		return (caddr_t)&preload_size;
	default:
		abort();
	}
}
static int nlookup_init(struct nlookupdata *nd, const char *path, int seg,
			int flags)
{
	nd->nl_open_vp = NULL;
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
		if (!strcmp(path, files[i].path)) {
			nd->nl_open_vp = &files[i];
			return 0;
		}
	return ENOENT;
}
static int vn_open(struct nlookupdata *nd, void *fp, int flags, int mode)
{
	return 0;
}
static void nlookup_done(struct nlookupdata *nd) {}
static int VOP_GETATTR(struct vnode *vp, struct vattr *attr)
{
	attr->va_size = vp->size;
	return 0;
}
static int vn_rdwr(int op, struct vnode *vp, void *data, size_t size,
		   off_t offset, int seg, int flags, void *cred, int *resid)
{
	assert(cred == proc0.p_ucred);
	memcpy(data, vp->contents, size);
	*resid = short_read ? 1 : 0;
	return read_error;
}
static void vn_unlock(struct vnode *vp) {}
static int vn_close(struct vnode *vp, int flags, void *fp) { return 0; }
static int linker_reference_module(const char *, void *, linker_file_t *);
static int linker_release_module(const char *, void *, linker_file_t);

/* SOURCE_UNDER_TEST */

static int linker_reference_module(const char *name, void *unused,
				   linker_file_t *file)
{
	++module_loads;
	if (strcmp(name, "module.bin"))
		return ENOENT;
	assert(firmware_register("module.bin", "MODULE", 6, 1, NULL));
	*file = (void *)1;
	return 0;
}
static int linker_release_module(const char *name, void *unused,
				 linker_file_t file)
{
	++module_releases;
	return firmware_unregister(name != NULL ? name : "module.bin");
}
static void release(const struct firmware *fw)
{
	firmware_put(fw, FIRMWARE_UNLOAD);
	if (pending)
		run_task();
}
static void assert_bytes(const struct firmware *fw, const char *bytes)
{
	assert(fw != NULL);
	assert(fw->datasize == strlen(bytes));
	assert(!memcmp(fw->data, bytes, fw->datasize));
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	strcpy(firmware_path, "/first;/second");
	TASK_INIT(&firmware_unload_task, 0, unloadentry, NULL);
	const char *test = argv[1];
	const struct firmware *first, *second;
	char name[128];
	if (!strcmp(test, "file_names")) {
		strcpy(name, "load.bin");
		first = firmware_get(name);
		assert_bytes(first, "LOAD");
		strcpy(name, "unload.bin");
		second = firmware_get(name);
		assert_bytes(second, "STOP");
		memset(name, 'x', sizeof(name));
		assert(!strcmp(first->name, "load.bin"));
		assert(!strcmp(second->name, "unload.bin"));
		release(first);
		release(second);
		assert(allocations == 0);
	} else if (!strcmp(test, "preload_names")) {
		preload_enabled = true;
		cold = 1;
		strcpy(name, "chip/preload.bin");
		first = firmware_get(name);
		assert(first != NULL);
		strcpy(name, "changed");
		assert(!strcmp(first->name, "chip/preload.bin"));
		assert(first->data == preload_bytes);
		assert(allocations == 0);
		release(first);
		assert(lookup("chip/preload.bin", NULL));
		assert(firmware_unregister("chip/preload.bin") == 0);
	} else if (!strcmp(test, "references")) {
		first = firmware_get("load.bin");
		second = firmware_get("load.bin");
		assert(first == second);
		assert(firmware_unregister("load.bin") == EBUSY);
		firmware_put(first, 0);
		assert(pending == NULL);
		release(second);
		assert(lookup("load.bin", NULL) == NULL);
		assert(allocations == 0);
	} else if (!strcmp(test, "reacquire")) {
		first = firmware_get("load.bin");
		firmware_put(first, FIRMWARE_UNLOAD);
		second = firmware_get("load.bin");
		assert(first == second);
		run_task();
		assert_bytes(second, "LOAD");
		release(second);
		assert(allocations == 0);
	} else if (!strcmp(test, "size_limit")) {
		firmware_max_size = ULONG_MAX;
		files[4].size = (size_t)INT_MAX + 1;
		assert(!firmware_get("large.bin"));
		assert(allocations == 0);
	} else if (!strcmp(test, "preload_boundary")) {
		preload_enabled = true;
		cold = 1;
		assert(!firmware_get("hip/preload.bin"));
		assert(!firmware_get("missing/preload.bin"));
		first = firmware_get("chip/preload.bin");
		assert(first && first->data == preload_bytes);
		release(first);
		assert(firmware_unregister("chip/preload.bin") == 0);
		assert(allocations == 0);
	} else if (!strcmp(test, "file_errors")) {
		firmware_max_size = 4;
		assert(!firmware_get("large.bin"));
		assert(!firmware_get("empty.bin"));
		assert(!firmware_get("directory"));
		short_read = 1;
		assert(!firmware_get("load.bin"));
		short_read = 0;
		read_error = EIO;
		assert(!firmware_get("load.bin"));
		assert(allocations == 0);
	} else if (!strcmp(test, "allocation_failure")) {
		for (int failure = 1; failure <= 3; ++failure) {
			allocation_count = 0;
			fail_allocation = failure;
			first = firmware_get("load.bin");
			assert(first == NULL);
			assert(allocations == 0);
		}
	} else if (!strcmp(test, "registry_full")) {
		char names[FIRMWARE_MAX][24];
		for (int i = 0; i < FIRMWARE_MAX; ++i) {
			snprintf(names[i], sizeof(names[i]), "static-%d", i);
			assert(firmware_register(names[i], "X", 1, 0, NULL));
		}
		assert(!firmware_get("load.bin"));
		assert(allocations == 0);
		for (int i = 0; i < FIRMWARE_MAX; ++i)
			assert(firmware_unregister(names[i]) == 0);
	} else if (!strcmp(test, "module_fallback")) {
		first = firmware_get("module.bin");
		assert_bytes(first, "MODULE");
		release(first);
		assert(module_loads == 1 && module_releases == 1);
		assert(lookup("module.bin", NULL) == NULL);
		assert(allocations == 0);
	} else if (!strcmp(test, "module_parent")) {
		first = firmware_register("parent", "P", 1, 0, NULL);
		assert(first);
		assert(firmware_register("child", "C", 1, 0, first));
		assert(firmware_unregister("parent") == EBUSY);
		assert(firmware_unregister("child") == 0);
		assert(firmware_unregister("parent") == 0);
	} else if (!strcmp(test, "search_order")) {
		first = firmware_get("load.bin");
		assert_bytes(first, "LOAD");
		release(first);
		strcpy(firmware_path, "/missing;;/second;/first");
		first = firmware_get("load.bin");
		assert_bytes(first, "NEXT");
		release(first);
		assert(module_loads == 0);
		assert(allocations == 0);
	} else
		abort();
	assert(!firmware_lock.held);
	return 0;
}
