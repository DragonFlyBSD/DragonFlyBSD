/*
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef	_LINUX_DEVICE_H_
#define	_LINUX_DEVICE_H_

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/atomic.h>
#include <linux/ratelimit.h>
#include <linux/gfp.h>

#include <sys/bus.h>

struct device {
	struct device	*parent;

	struct kobject kobj;

	device_t	bsddev;

	void		*driver_data;	/* for dev_set and get_drvdata */

	struct dev_pm_info power;
};

struct device_driver {
	const struct dev_pm_ops *pm;
};

struct device_node;

#define	dev_dbg(dev, fmt, ...)						\
	device_printf((dev)->bsddev, "debug: " fmt, ## __VA_ARGS__)
#define	dev_err(dev, fmt, ...)						\
	device_printf((dev)->bsddev, "error: " fmt, ## __VA_ARGS__)
#define	dev_warn(dev, fmt, ...)						\
	device_printf((dev)->bsddev, "warning: " fmt, ## __VA_ARGS__)
#define	dev_info(dev, fmt, ...)						\
	device_printf(((struct device *)(dev))->bsddev, "info: " fmt, ## __VA_ARGS__)
#define dev_notice(dev, fmt, ...)					\
	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)

static inline void
dev_printk(const char *level, const struct device *dev, const char *fmt, ...)
{
	__va_list ap;

	device_printf((dev)->bsddev, "%s: ", level);
	__va_start(ap, fmt);
	kprintf(fmt, ap);
	__va_end(ap);
}

static inline const char *
dev_name(const struct device *dev)
{
	return("dev_name");
}

static inline void
dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

static inline void *
dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

static inline int
dev_set_name(struct device *dev, const char *name, ...)
{
	return 0;
}

#endif	/* _LINUX_DEVICE_H_ */
