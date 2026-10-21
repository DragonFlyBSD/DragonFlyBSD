/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver-local logging for the native NVIDIA GPU driver.
 */

#include "nvgpu_debug.h"

#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/sysctl.h>

/* Tracing is opt-in even when the kernel auto-enables selected KTR groups. */
#undef KTR_AUTO_ENABLE
#define KTR_AUTO_ENABLE 0
KTR_INFO_MASTER(nvgpu);

int nvgpu_debug = 0;
TUNABLE_INT("hw.nvgpu.debug", &nvgpu_debug);

static struct sysctl_ctx_list nvgpu_debug_sysctl_ctx;
static struct sysctl_oid *nvgpu_debug_sysctl_tree;
static bool nvgpu_debug_sysctl_ready;

/* Publish module sysctls.  The tunable above still controls pre-sysctl debug. */
int
nvgpu_debug_init(void)
{
	int error;

	error = sysctl_ctx_init(&nvgpu_debug_sysctl_ctx);
	if (error != 0)
		return (error);
	nvgpu_debug_sysctl_tree = SYSCTL_ADD_NODE(&nvgpu_debug_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO, "nvgpu", CTLFLAG_RW,
	    NULL, "NVGPU driver");
	if (nvgpu_debug_sysctl_tree == NULL) {
		sysctl_ctx_free(&nvgpu_debug_sysctl_ctx);
		return (ENOMEM);
	}
	if (SYSCTL_ADD_INT(&nvgpu_debug_sysctl_ctx,
	    SYSCTL_CHILDREN(nvgpu_debug_sysctl_tree), OID_AUTO, "debug",
	    CTLFLAG_RW, &nvgpu_debug, 0, "Enable nvgpu debug logs") == NULL) {
		sysctl_ctx_free(&nvgpu_debug_sysctl_ctx);
		nvgpu_debug_sysctl_tree = NULL;
		return (ENOMEM);
	}
	nvgpu_debug_sysctl_ready = true;
	return (0);
}

/* Remove module sysctls before the KLD text is unloaded. */
void
nvgpu_debug_fini(void)
{
	if (!nvgpu_debug_sysctl_ready)
		return;
	sysctl_ctx_free(&nvgpu_debug_sysctl_ctx);
	nvgpu_debug_sysctl_tree = NULL;
	nvgpu_debug_sysctl_ready = false;
}

static const char *
nvgpu_debug_get_level_name(enum nvgpu_log_level level)
{
	switch (level) {
	case NVGPU_LOG_DEBUG:
		return ("debug");
	case NVGPU_LOG_INFO:
		return ("info");
	default:
		return ("log");
	}
}

static int
nvgpu_debug_should_log(enum nvgpu_log_level level)
{
	switch (level) {
	case NVGPU_LOG_DEBUG:
		return (nvgpu_debug != 0);
	case NVGPU_LOG_INFO:
		return (1);
	default:
		return (1);
	}
}

/* Format and emit one already-authorized log message. */
static void
nvgpu_debug_emit_vlog(enum nvgpu_log_level level, const char *file, const char *func,
	int line, const char *fmt, __va_list ap)
{
	char buf[512];
	const char *name;

	name = "nvgpu";
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	kprintf("%s: %s: %s:%s:%d: %s", name, nvgpu_debug_get_level_name(level),
	    file, func, line, buf);
}

/* Emit one driver log message with call-site metadata. */
/* Backend for nvgpu_log(); use the macro so call-site location is preserved. */
void
nvgpu_debug_emit_log(enum nvgpu_log_level level, const char *file, const char *func,
	int line, const char *fmt, ...)
{
	__va_list ap;

	if (!nvgpu_debug_should_log(level))
		return;
	__va_start(ap, fmt);
	nvgpu_debug_emit_vlog(level, file, func, line, fmt, ap);
	__va_end(ap);
}
