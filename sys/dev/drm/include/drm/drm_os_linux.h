/**
 * \file drm_os_dragonfly.h
 * OS abstraction macros.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/serialize.h>

/* Handle the DRM options from kernel config. */
#ifdef __DragonFly__
#include "opt_drm.h"

#ifdef DRM_DEBUG
#  if DRM_DEBUG>1
#    define DRM_DEBUG_DEFAULT_ON 2
#  else
#    define DRM_DEBUG_DEFAULT_ON 1
#  endif
#undef DRM_DEBUG
/* XXX disable DRM_LINUX for now to unbreak LINT64 */
#undef DRM_LINUX
#endif /* DRM_DEBUG */
#endif

/** Current process ID */
#define DRM_CURRENTPID		(curproc != NULL ? curproc->p_pid : -1)
#define DRM_UDELAY(d)		DELAY(d)
/** Read a byte from a MMIO region */
#define DRM_READ8(map, offset)						\
	*(volatile u_int8_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset))
#define DRM_READ16(map, offset)						\
	le16toh(*(volatile u_int16_t *)(((vm_offset_t)(map)->handle) +	\
	    (vm_offset_t)(offset)))
#define DRM_READ32(map, offset)						\
	le32toh(*(volatile u_int32_t *)(((vm_offset_t)(map)->handle) +	\
	    (vm_offset_t)(offset)))
#define DRM_READ64(map, offset)						\
	le64toh(*(volatile u_int64_t *)(((vm_offset_t)(map)->handle) +	\
	    (vm_offset_t)(offset)))
#define DRM_WRITE8(map, offset, val)					\
	*(volatile u_int8_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset)) = val
#define DRM_WRITE16(map, offset, val)					\
	*(volatile u_int16_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset)) = htole16(val)

#define DRM_WRITE32(map, offset, val)					\
	*(volatile u_int32_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset)) = htole32(val)

#define DRM_WRITE64(map, offset, val)					\
	*(volatile u_int64_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset)) = htole64(val)

/* Returns -errno to shared code */
#define DRM_WAIT_ON( ret, queue, timeout, condition )		\
for ( ret = 0 ; !ret && !(condition) ; ) {			\
	lwkt_serialize_enter(&dev->irq_lock);			\
	if (!(condition)) {					\
		tsleep_interlock(&(queue), PCATCH);		\
		lwkt_serialize_exit(&dev->irq_lock);		\
		ret = -tsleep(&(queue), PCATCH | PINTERLOCKED,	\
			  "drmwtq", (timeout));			\
	} else {						\
		lwkt_serialize_exit(&dev->irq_lock);		\
	}							\
}

/* include code to override EDID blocks from external firmware modules */
#define CONFIG_DRM_LOAD_EDID_FIRMWARE
