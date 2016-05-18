/*
 * Copyright (c) 2016 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_BUG_H_
#define _ASM_BUG_H_

#include <sys/param.h>
#include <linux/kernel.h>

#define BUG()	do				\
{						\
	panic("BUG in %s at %s:%u",		\
		__func__, __FILE__, __LINE__);	\
} while (0)

#define BUG_ON(condition)	do { if (condition) BUG(); } while(0)

#define _WARN_STR(x)	#x

#define WARN_ON(condition) ({						\
	int __ret = !!(condition);					\
	if (__ret)							\
		kprintf("WARNING %s failed at %s:%d\n",			\
		    _WARN_STR(condition), __FILE__, __LINE__);		\
	unlikely(__ret);						\
})

#ifndef WARN
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		pr_warning(format);					\
	unlikely(__ret_warn_on);					\
})
#endif

#define WARN_ON_ONCE(condition) ({					\
	static int __warned;						\
	int __ret = !!(condition);					\
	if (__ret && !__warned) {					\
		kprintf("WARNING %s failed at %s:%d\n",			\
		    _WARN_STR(condition), __FILE__, __LINE__);		\
		__warned = 1;						\
	}								\
	unlikely(__ret);						\
})

#define WARN_ONCE(condition, format...) ({				\
	static bool __warned_once;					\
	int __ret = !!(condition);					\
									\
	if ((condition) && !__warned_once) {				\
		WARN(condition, format);				\
		__warned_once = true;					\
	}								\
	unlikely(__ret);						\
})

#define WARN_ON_SMP(condition)	WARN_ON(condition)

#endif /* _ASM_BUG_H_ */
