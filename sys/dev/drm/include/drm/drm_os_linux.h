/**
 * \file drm_os_dragonfly.h
 * OS abstraction macros.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/serialize.h>
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

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
#endif /* DRM_DEBUG */
#endif /* DragonFly */

#ifndef readq
static inline u64 readq(void __iomem *reg)
{
	return ((u64) readl(reg)) | (((u64) readl(reg + 4UL)) << 32);
}

static inline void writeq(u64 val, void __iomem *reg)
{
	writel(val & 0xffffffff, reg);
	writel(val >> 32, reg + 0x4UL);
}
#endif

/** Current process ID */
#define DRM_CURRENTPID			(curproc != NULL ? curproc->p_pid : -1)
#define DRM_UDELAY(d)			DELAY(d)
/** Read a byte from a MMIO region */
#define DRM_READ8(map, offset)		readb(((void __iomem *)(map)->handle) + (offset))
/** Read a word from a MMIO region */
#define DRM_READ16(map, offset)         readw(((void __iomem *)(map)->handle) + (offset))
/** Read a dword from a MMIO region */
#define DRM_READ32(map, offset)		readl(((void __iomem *)(map)->handle) + (offset))
/** Write a byte into a MMIO region */
#define DRM_WRITE8(map, offset, val)	writeb(val, ((void __iomem *)(map)->handle) + (offset))
/** Write a word into a MMIO region */
#define DRM_WRITE16(map, offset, val)   writew(val, ((void __iomem *)(map)->handle) + (offset))
/** Write a dword into a MMIO region */
#define DRM_WRITE32(map, offset, val)					\
	*(volatile u_int32_t *)(((vm_offset_t)(map)->handle) +		\
	    (vm_offset_t)(offset)) = htole32(val)

/** Read a qword from a MMIO region - be careful using these unless you really understand them */
#define DRM_READ64(map, offset)		readq(((void __iomem *)(map)->handle) + (offset))
/** Write a qword into a MMIO region */
#define DRM_WRITE64(map, offset, val)	writeq(val, ((void __iomem *)(map)->handle) + (offset))

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
