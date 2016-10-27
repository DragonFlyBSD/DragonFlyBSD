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

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

static struct lock i2c_lock;
LOCK_SYSINIT(i2c_lock, &i2c_lock, "i2cl", LK_CANRECURSE);

int
i2c_add_adapter(struct i2c_adapter *adapter)
{
	/* Linux registers a unique bus number here */
	return 0;
}

void
i2c_del_adapter(struct i2c_adapter *adapter)
{
	/* Linux deletes a unique bus number here */
}

/*
 * i2c_transfer()
 * The original Linux implementation does:
 * 1. return -EOPNOTSUPP if adapter->algo->master_xfer is NULL
 * 2. try to transfer msgs by calling adapter->algo->master_xfer()
 * 3. if it took more ticks than adapter->timeout, fail
 * 4. if the transfer failed, retry up to adapter->retries times
 * 5. return the result of the last call of adapter->algo->master_xfer()
 */
int
i2c_transfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	uint64_t start_ticks;
	int ret, tries = 0;

	if (adapter->algo->master_xfer == NULL)
		return -EOPNOTSUPP;

	lockmgr(&i2c_lock, LK_EXCLUSIVE);
	start_ticks = ticks;
	do {
		ret = adapter->algo->master_xfer(adapter, msgs, num);
		if (ticks > start_ticks + adapter->timeout)
			break;
		if (ret != -EAGAIN)
			break;
		tries++;
	} while (tries < adapter->retries);
	lockmgr(&i2c_lock, LK_RELEASE);

	return ret;
}

static int
bit_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
	/* XXX Linux really does try to transfer some data here */
	return 0;
}

static uint32_t
bit_func(struct i2c_adapter *adap)
{
	return (I2C_FUNC_I2C | I2C_FUNC_NOSTART | I2C_FUNC_SMBUS_EMUL |
		I2C_FUNC_SMBUS_READ_BLOCK_DATA |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING);
}

const struct i2c_algorithm i2c_bit_algo = {
	.master_xfer	= bit_xfer,
	.functionality	= bit_func,
};
