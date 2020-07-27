/**
 * \file drm_os_linux.h
 * OS abstraction macros.
 */

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/sched/signal.h>
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
#else /* !DRM_DEBUG */
#  define DRM_DEBUG_DEFAULT_ON 0
#endif /* DRM_DEBUG */
#endif /* __DragonFly__ */

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
#define DRM_WRITE32(map, offset, val)	writel(val, ((void __iomem *)(map)->handle) + (offset))

/** Read a qword from a MMIO region - be careful using these unless you really understand them */
#define DRM_READ64(map, offset)		readq(((void __iomem *)(map)->handle) + (offset))
/** Write a qword into a MMIO region */
#define DRM_WRITE64(map, offset, val)	writeq(val, ((void __iomem *)(map)->handle) + (offset))

#define DRM_WAIT_ON( ret, queue, timeout, condition )		\
do {								\
	wait_queue_t entry = {					\
		.private	= current,			\
		.func		= default_wake_function,	\
	};							\
	unsigned long end = jiffies + (timeout);		\
	add_wait_queue(&(queue), &entry);			\
								\
	for (;;) {						\
		__set_current_state(TASK_INTERRUPTIBLE);	\
		if (condition)					\
			break;					\
		if (time_after_eq(jiffies, end)) {		\
			ret = -EBUSY;				\
			break;					\
		}						\
		schedule_timeout((HZ/100 > 1) ? HZ/100 : 1);	\
		if (signal_pending(current)) {			\
			ret = -EINTR;				\
			break;					\
		}						\
	}							\
	__set_current_state(TASK_RUNNING);			\
	remove_wait_queue(&(queue), &entry);			\
} while (0)
