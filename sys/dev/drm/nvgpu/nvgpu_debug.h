/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver-local logging for the native NVIDIA GPU driver.
 */

#ifndef _NVGPU_DEBUG_H_
#define _NVGPU_DEBUG_H_

#include <sys/param.h>
#include <sys/systm.h>

enum nvgpu_log_level {
	NVGPU_LOG_DEBUG,
	NVGPU_LOG_INFO,
};

extern int nvgpu_debug;

/* Create and remove the module-owned hw.nvgpu sysctl nodes. */
int nvgpu_debug_init(void);
void nvgpu_debug_fini(void);

/* Backend for nvgpu_log(); use the macro so call-site location is preserved. */
void nvgpu_debug_emit_log(enum nvgpu_log_level level, const char *file,
	const char *func, int line, const char *fmt, ...) __printflike(5, 6);

/* Log with call-site file/function/line. */
#define nvgpu_log(level, fmt, ...) \
	nvgpu_debug_emit_log((level), __FILE__, __func__, __LINE__, (fmt), \
	    ##__VA_ARGS__)

#endif /* _NVGPU_DEBUG_H_ */
