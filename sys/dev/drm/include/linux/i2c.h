/*
 * Copyright (c) 2013-2019 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_I2C_H_
#define _LINUX_I2C_H_

#include <linux/mod_devicetable.h>
#include <linux/device.h>	/* for struct device */
#include <linux/sched.h>	/* for completion */
#include <linux/mutex.h>
#include <linux/of.h>		/* for struct device_node */
#include <uapi/linux/i2c.h>

#include <bus/iicbus/iic.h>

#define I2C_M_RD	IIC_M_RD
#define I2C_M_NOSTART	IIC_M_NOSTART

struct i2c_lock_operations;

struct i2c_adapter {
	const struct i2c_algorithm *algo;
	void *algo_data;

	int timeout;
	int retries;
	struct device dev;
	void *private_data;

	char name[48];

	const struct i2c_lock_operations *lock_ops;
};

struct i2c_client {
	unsigned short flags;
	unsigned short addr;
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;
};

extern int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			int num);

struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num);

	u32 (*functionality) (struct i2c_adapter *);
};

extern int i2c_add_adapter(struct i2c_adapter *);
extern void i2c_del_adapter(struct i2c_adapter *);

static inline void
i2c_set_adapdata(struct i2c_adapter *dev, void *data)
{
	dev->private_data = data;
}

static inline void *
i2c_get_adapdata(const struct i2c_adapter *dev)
{
	return dev->private_data;
}

struct i2c_board_info {
	char		type[I2C_NAME_SIZE];
	unsigned short	addr;
};

struct i2c_client *
i2c_new_device(struct i2c_adapter *adap, struct i2c_board_info const *info);

#define I2C_LOCK_SEGMENT      BIT(1)

struct i2c_lock_operations {
	void (*lock_bus)(struct i2c_adapter *, unsigned int flags);
	int (*trylock_bus)(struct i2c_adapter *, unsigned int flags);
	void (*unlock_bus)(struct i2c_adapter *, unsigned int flags);
};

#endif	/* _LINUX_I2C_H_ */
