/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Intel 4th generation mobile cpus integrated I2C device, smbus driver.
 *
 * See ig4_reg.h for datasheet reference and notes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <sys/rman.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/smbus/smbconf.h>

#include "smbus_if.h"

#include "ig4_reg.h"
#include "ig4_var.h"

#define TRANS_NORMAL	1
#define TRANS_PCALL	2
#define TRANS_BLOCK	3

static void ig4iic_intr(void *cookie);

/*
 * Low-level inline support functions
 */
static __inline
void
reg_write(ig4iic_softc_t *sc, uint32_t reg, uint32_t value)
{
	bus_space_write_4(sc->regs_t, sc->regs_h, reg, value);
	bus_space_barrier(sc->regs_t, sc->regs_h, reg, 4,
			  BUS_SPACE_BARRIER_WRITE);
}

static __inline
uint32_t
reg_read(ig4iic_softc_t *sc, uint32_t reg)
{
	uint32_t value;

	bus_space_barrier(sc->regs_t, sc->regs_h, reg, 4,
			  BUS_SPACE_BARRIER_READ);
	value = bus_space_read_4(sc->regs_t, sc->regs_h, reg);
	return value;
}

/*
 * Enable or disable the controller and wait for the controller to acknowledge
 * the state change.
 */
static
int
set_controller(ig4iic_softc_t *sc, uint32_t ctl)
{
	int retry;
	int error;
	uint32_t v;

	reg_write(sc, IG4_REG_I2C_EN, ctl);
	error = SMB_ETIMEOUT;

	for (retry = 100; retry > 0; --retry) {
		v = reg_read(sc, IG4_REG_ENABLE_STATUS);
		if (((v ^ ctl) & IG4_I2C_ENABLE) == 0) {
			error = 0;
			break;
		}
		tsleep(sc, 0, "i2cslv", 1);
	}
	return error;
}

/*
 * Wait up to 25ms for the requested status using a 25uS polling loop.
 */
static
int
wait_status(ig4iic_softc_t *sc, uint32_t status)
{
	uint32_t v;
	int error;
	int txlvl = -1;
	sysclock_t count;
	sysclock_t limit;

	error = SMB_ETIMEOUT;
	count = sys_cputimer->count();
	limit = sys_cputimer->freq / 40;

	while (sys_cputimer->count() - count <= limit) {
		v = reg_read(sc, IG4_REG_I2C_STA);
		if (v & status) {
			error = 0;
			break;
		}

		if (status & IG4_STATUS_TX_EMPTY) {
			v = reg_read(sc, IG4_REG_TXFLR) & IG4_FIFOLVL_MASK;
			if (txlvl != v) {
				txlvl = v;
				count = sys_cputimer->count();
			}
		}

		if (status & IG4_STATUS_RX_NOTEMPTY) {
			lksleep(sc, &sc->lk, 0, "i2cwait", (hz + 99) / 100);
		} else {
			DELAY(25);
		}
	}
	return error;
}

/*
 * Set the slave address.  The controller must be disabled when
 * changing the address.
 *
 * This operation does not issue anything to the I2C bus but sets
 * the target address for when the controller later issues a START.
 */
static
void
set_slave_addr(ig4iic_softc_t *sc, uint8_t slave, int trans_op)
{
	uint32_t tar;
	uint32_t ctl;
	int use_10bit;

	use_10bit = sc->use_10bit;
	if (trans_op & SMB_TRANS_7BIT)
		use_10bit = 0;
	if (trans_op & SMB_TRANS_10BIT)
		use_10bit = 1;

	if (sc->slave_valid && sc->last_slave == slave &&
	    sc->use_10bit == use_10bit) {
		return;
	}
	sc->use_10bit = use_10bit;

	/*
	 * Wait for TXFIFO to drain before disabling the controller.
	 *
	 * If a write message has not been completed it's really a
	 * programming error, but for now in that case issue an extra
	 * byte + STOP.
	 *
	 * If a read message has not been completed it's also a programming
	 * error, for now just ignore it.
	 */
	wait_status(sc, IG4_STATUS_TX_NOTFULL);
	if (sc->write_started) {
		reg_write(sc, IG4_REG_DATA_CMD, IG4_DATA_STOP);
		sc->write_started = 0;
	}
	if (sc->read_started)
		sc->read_started = 0;
	wait_status(sc, IG4_STATUS_TX_EMPTY);

	set_controller(sc, 0);
	ctl = reg_read(sc, IG4_REG_CTL);
	ctl &= ~IG4_CTL_10BIT;
	ctl |= IG4_CTL_RESTARTEN;

	tar = slave;
	if (sc->use_10bit) {
		tar |= IG4_TAR_10BIT;
		ctl |= IG4_CTL_10BIT;
	}
	reg_write(sc, IG4_REG_CTL, ctl);
	reg_write(sc, IG4_REG_TAR_ADD, tar);
	set_controller(sc, IG4_I2C_ENABLE);
	sc->slave_valid = 1;
	sc->last_slave = slave;
}

/*
 * Issue START with byte command, possible count, and a variable length
 * read or write buffer, then possible turn-around read.  The read also
 * has a possible count received.
 *
 * For SMBUS -
 *
 * Quick:		START+ADDR+RD/WR STOP
 *
 * Normal:		START+ADDR+WR COMM DATA..DATA STOP
 *
 *			START+ADDR+RD COMM
 *			RESTART+ADDR RDATA..RDATA STOP
 *			(can also be used for I2C transactions)
 *
 * Process Call:	START+ADDR+WR COMM DATAL DATAH
 *			RESTART+ADDR+RD RDATAL RDATAH STOP
 *
 * Block:		START+ADDR+RD COMM
 *			RESTART+ADDR+RD RCOUNT DATA... STOP
 *
 * 			START+ADDR+WR COMM
 *			RESTART+ADDR+WR WCOUNT DATA... STOP
 *
 * For I2C - basically, no *COUNT fields.
 *
 * Generally speaking, the START+ADDR / RESTART+ADDR is handled automatically
 * by the controller at the beginning of a command sequence or on a data
 * direction turn-around, and we only need to tell it when to issue the STOP.
 */
static int
smb_transaction(ig4iic_softc_t *sc, char cmd, int op,
		char *wbuf, int wcount, char *rbuf, int rcount, int *actualp)
{
	int error;
	uint32_t last;

	/*
	 * Issue START or RESTART with next data byte, clear any previous
	 * abort condition that may have been holding the txfifo in reset.
	 */
	last = IG4_DATA_RESTART;
	reg_read(sc, IG4_REG_CLR_TX_ABORT);
	if (actualp)
		*actualp = 0;

	/*
	 * Issue command if not told otherwise (smbus).
	 */
	if ((op & SMB_TRANS_NOCMD) == 0) {
		error = wait_status(sc, IG4_STATUS_TX_NOTFULL);
		if (error)
			goto done;
		last |= (u_char)cmd;
		if (wcount == 0 && rcount == 0 && (op & SMB_TRANS_NOSTOP) == 0)
			last |= IG4_DATA_STOP;
		reg_write(sc, IG4_REG_DATA_CMD, last);
		last = 0;
	}

	/*
	 * If writing and not told otherwise, issue the write count (smbus).
	 */
	if (wcount && (op & SMB_TRANS_NOCNT) == 0) {
		error = wait_status(sc, IG4_STATUS_TX_NOTFULL);
		if (error)
			goto done;
		last |= (u_char)cmd;
		reg_write(sc, IG4_REG_DATA_CMD, last);
		last = 0;
	}

	/*
	 * Bulk write (i2c)
	 */
	while (wcount) {
		error = wait_status(sc, IG4_STATUS_TX_NOTFULL);
		if (error)
			goto done;
		last |= (u_char)*wbuf;
		if (wcount == 1 && rcount == 0 && (op & SMB_TRANS_NOSTOP) == 0)
			last |= IG4_DATA_STOP;
		reg_write(sc, IG4_REG_DATA_CMD, last);
		--wcount;
		++wbuf;
		last = 0;
	}

	/*
	 * Issue reads to xmit FIFO (strange, I know) to tell the controller
	 * to clock in data.  At the moment just issue one read ahead to
	 * pipeline the incoming data.
	 *
	 * NOTE: In the case of NOCMD and wcount == 0 we still issue a
	 *	 RESTART here, even if the data direction has not changed
	 *	 from the previous CHAINing call.  This we force the RESTART.
	 *	 (A new START is issued automatically by the controller in
	 *	 the other nominal cases such as a data direction change or
	 *	 a previous STOP was issued).
	 *
	 * If this will be the last byte read we must also issue the STOP
	 * at the end of the read.
	 */
	if (rcount) {
		last = IG4_DATA_RESTART | IG4_DATA_COMMAND_RD;
		if (rcount == 1 &&
		    (op & (SMB_TRANS_NOSTOP | SMB_TRANS_NOCNT)) ==
		    SMB_TRANS_NOCNT) {
			last |= IG4_DATA_STOP;
		}
		reg_write(sc, IG4_REG_DATA_CMD, last);
		last = IG4_DATA_COMMAND_RD;
	}

	/*
	 * Bulk read (i2c) and count field handling (smbus)
	 */
	while (rcount) {
		/*
		 * Maintain a pipeline by queueing the allowance for the next
		 * read before waiting for the current read.
		 */
		if (rcount > 1) {
			if (op & SMB_TRANS_NOCNT)
				last = (rcount == 2) ? IG4_DATA_STOP : 0;
			else
				last = 0;
			reg_write(sc, IG4_REG_DATA_CMD, IG4_DATA_COMMAND_RD |
							last);
		}
		error = wait_status(sc, IG4_STATUS_RX_NOTEMPTY);
		if (error)
			goto done;
		last = reg_read(sc, IG4_REG_DATA_CMD);

		if (op & SMB_TRANS_NOCNT) {
			*rbuf = (u_char)last;
			++rbuf;
			--rcount;
			if (actualp)
				++*actualp;
		} else {
			/*
			 * Handle count field (smbus), which is not part of
			 * the rcount'ed buffer.  The first read data in a
			 * bulk transfer is the count.
			 *
			 * XXX if rcount is loaded as 0 how do I generate a
			 *     STOP now without issuing another RD or WR?
			 */
			if (rcount > (u_char)last)
				rcount = (u_char)last;
			op |= SMB_TRANS_NOCNT;
		}
	}
	error = 0;
done:
	/* XXX wait for xmit buffer to become empty */
	last = reg_read(sc, IG4_REG_TX_ABRT_SOURCE);

	return error;
}

/*
 *				SMBUS API FUNCTIONS
 *
 * Called from ig4iic_pci_attach/detach()
 */
int
ig4iic_attach(ig4iic_softc_t *sc)
{
	int error;
	uint32_t v;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	v = reg_read(sc, IG4_REG_COMP_TYPE);
	kprintf("type %08x\n", v);
	v = reg_read(sc, IG4_REG_COMP_PARAM1);
	kprintf("params %08x\n", v);
	v = reg_read(sc, IG4_REG_COMP_VER);
	kprintf("version %08x\n", v);
	if (v != IG4_COMP_VER) {
		error = ENXIO;
		goto done;
	}
#if 1
	v = reg_read(sc, IG4_REG_SS_SCL_HCNT);
	kprintf("SS_SCL_HCNT %08x\n", v);
	v = reg_read(sc, IG4_REG_SS_SCL_LCNT);
	kprintf("SS_SCL_LCNT %08x\n", v);
	v = reg_read(sc, IG4_REG_FS_SCL_HCNT);
	kprintf("FS_SCL_HCNT %08x\n", v);
	v = reg_read(sc, IG4_REG_FS_SCL_LCNT);
	kprintf("FS_SCL_LCNT %08x\n", v);
	v = reg_read(sc, IG4_REG_SDA_HOLD);
	kprintf("HOLD        %08x\n", v);

	v = reg_read(sc, IG4_REG_SS_SCL_HCNT);
	reg_write(sc, IG4_REG_FS_SCL_HCNT, v);
	v = reg_read(sc, IG4_REG_SS_SCL_LCNT);
	reg_write(sc, IG4_REG_FS_SCL_LCNT, v);
#endif
	/*
	 * Program based on a 25000 Hz clock.  This is a bit of a
	 * hack (obviously).  The defaults are 400 and 470 for standard
	 * and 60 and 130 for fast.  The defaults for standard fail
	 * utterly (presumably cause an abort) because the clock time
	 * is ~18.8ms by default.  This brings it down to ~4ms (for now).
	 */
	reg_write(sc, IG4_REG_SS_SCL_HCNT, 100);
	reg_write(sc, IG4_REG_SS_SCL_LCNT, 125);
	reg_write(sc, IG4_REG_FS_SCL_HCNT, 100);
	reg_write(sc, IG4_REG_FS_SCL_LCNT, 125);

	/*
	 * Use a threshold of 1 so we get interrupted on each character,
	 * allowing us to use lksleep() in our poll code.  Not perfect
	 * but this is better than using DELAY() for receiving data.
	 */
	reg_write(sc, IG4_REG_RX_TL, 1);

	reg_write(sc, IG4_REG_CTL,
		  IG4_CTL_MASTER |
		  IG4_CTL_SLAVE_DISABLE |
		  IG4_CTL_RESTARTEN |
		  IG4_CTL_SPEED_STD);

	sc->smb = device_add_child(sc->dev, "smbus", -1);
	if (sc->smb == NULL) {
		device_printf(sc->dev, "smbus driver not found\n");
		error = ENXIO;
		goto done;
	}

#if 0
	/*
	 * Don't do this, it blows up the PCI config
	 */
	reg_write(sc, IG4_REG_RESETS, IG4_RESETS_ASSERT);
	reg_write(sc, IG4_REG_RESETS, IG4_RESETS_DEASSERT);
#endif

	/*
	 * Interrupt on STOP detect or receive character ready
	 */
	reg_write(sc, IG4_REG_INTR_MASK, IG4_INTR_STOP_DET |
					 IG4_INTR_RX_FULL);
	if (set_controller(sc, 0))
		device_printf(sc->dev, "controller error during attach-1\n");
	if (set_controller(sc, IG4_I2C_ENABLE))
		device_printf(sc->dev, "controller error during attach-2\n");
	error = bus_setup_intr(sc->dev, sc->intr_res, 0,
			       ig4iic_intr, sc, &sc->intr_handle, NULL);
	if (error) {
		device_printf(sc->dev,
			      "Unable to setup irq: error %d\n", error);
		goto done;
	}

	/* Attach us to the smbus */
	lockmgr(&sc->lk, LK_RELEASE);
	error = bus_generic_attach(sc->dev);
	lockmgr(&sc->lk, LK_EXCLUSIVE);
	if (error) {
		device_printf(sc->dev,
			      "failed to attach child: error %d\n", error);
		goto done;
	}
	sc->generic_attached = 1;

done:
	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

int
ig4iic_detach(ig4iic_softc_t *sc)
{
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	reg_write(sc, IG4_REG_INTR_MASK, 0);
	reg_read(sc, IG4_REG_CLR_INTR);
	set_controller(sc, 0);

	if (sc->generic_attached) {
		error = bus_generic_detach(sc->dev);
		if (error)
			goto done;
		sc->generic_attached = 0;
	}
	if (sc->smb) {
		device_delete_child(sc->dev, sc->smb);
		sc->smb = NULL;
	}
	if (sc->intr_handle) {
		bus_teardown_intr(sc->dev, sc->intr_res, sc->intr_handle);
		sc->intr_handle = NULL;
	}

	error = 0;
done:
	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

int
ig4iic_smb_callback(device_t dev, int index, void *data)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	switch (index) {
	case SMB_REQUEST_BUS:
		error = 0;
		break;
	case SMB_RELEASE_BUS:
		error = 0;
		break;
	default:
		error = SMB_EABORT;
		break;
	}

	lockmgr(&sc->lk, LK_RELEASE);

	return error;
}

/*
 * Quick command.  i.e. START + cmd + R/W + STOP and no data.  It is
 * unclear to me how I could implement this with the intel i2c controller
 * because the controler sends STARTs and STOPs automatically with data.
 */
int
ig4iic_smb_quick(device_t dev, u_char slave, int how)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	switch (how) {
	case SMB_QREAD:
		error = SMB_ENOTSUPP;
		break;
	case SMB_QWRITE:
		error = SMB_ENOTSUPP;
		break;
	default:
		error = SMB_ENOTSUPP;
		break;
	}
	lockmgr(&sc->lk, LK_RELEASE);

	return error;
}

/*
 * Incremental send byte without stop (?).  It is unclear why the slave
 * address is specified if this presumably is used in combination with
 * ig4iic_smb_quick().
 *
 * (Also, how would this work anyway?  Issue the last byte with writeb()?)
 */
int
ig4iic_smb_sendb(device_t dev, u_char slave, char byte)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	uint32_t cmd;
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	cmd = byte;
	if (wait_status(sc, IG4_STATUS_TX_NOTFULL) == 0) {
		reg_write(sc, IG4_REG_DATA_CMD, cmd);
		error = 0;
	} else {
		error = SMB_ETIMEOUT;
	}

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * Incremental receive byte without stop (?).  It is unclear why the slave
 * address is specified if this presumably is used in combination with
 * ig4iic_smb_quick().
 */
int
ig4iic_smb_recvb(device_t dev, u_char slave, char *byte)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	reg_write(sc, IG4_REG_DATA_CMD, IG4_DATA_COMMAND_RD);
	if (wait_status(sc, IG4_STATUS_RX_NOTEMPTY) == 0) {
		*byte = (uint8_t)reg_read(sc, IG4_REG_DATA_CMD);
		error = 0;
	} else {
		*byte = 0;
		error = SMB_ETIMEOUT;
	}

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * Write command and single byte in transaction.
 */
int
ig4iic_smb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	error = smb_transaction(sc, cmd, SMB_TRANS_NOCNT,
				&byte, 1, NULL, 0, NULL);

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * Write command and single word in transaction.
 */
int
ig4iic_smb_writew(device_t dev, u_char slave, char cmd, short word)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	char buf[2];
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	buf[0] = word & 0xFF;
	buf[1] = word >> 8;
	error = smb_transaction(sc, cmd, SMB_TRANS_NOCNT,
				buf, 2, NULL, 0, NULL);

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * write command and read single byte in transaction.
 */
int
ig4iic_smb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	error = smb_transaction(sc, cmd, SMB_TRANS_NOCNT,
				NULL, 0, byte, 1, NULL);

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * write command and read word in transaction.
 */
int
ig4iic_smb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	char buf[2];
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	if ((error = smb_transaction(sc, cmd, SMB_TRANS_NOCNT,
				     NULL, 0, buf, 2, NULL)) == 0) {
		*word = (u_char)buf[0] | ((u_char)buf[1] << 8);
	}

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * write command and word and read word in transaction
 */
int
ig4iic_smb_pcall(device_t dev, u_char slave, char cmd,
		 short sdata, short *rdata)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	char rbuf[2];
	char wbuf[2];
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	wbuf[0] = sdata & 0xFF;
	wbuf[1] = sdata >> 8;
	if ((error = smb_transaction(sc, cmd, SMB_TRANS_NOCNT,
				     wbuf, 2, rbuf, 2, NULL)) == 0) {
		*rdata = (u_char)rbuf[0] | ((u_char)rbuf[1] << 8);
	}

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

int
ig4iic_smb_bwrite(device_t dev, u_char slave, char cmd,
		  u_char wcount, char *buf)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	error = smb_transaction(sc, cmd, 0,
				buf, wcount, NULL, 0, NULL);

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

int
ig4iic_smb_bread(device_t dev, u_char slave, char cmd,
		 u_char *countp_char, char *buf)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int rcount = *countp_char;
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, 0);
	error = smb_transaction(sc, cmd, 0,
				NULL, 0, buf, rcount, &rcount);
	*countp_char = rcount;

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

int
ig4iic_smb_trans(device_t dev, int slave, char cmd, int op,
		 char *wbuf, int wcount, char *rbuf, int rcount,
		 int *actualp)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	set_slave_addr(sc, slave, op);
	error = smb_transaction(sc, cmd, op,
				wbuf, wcount, rbuf, rcount, actualp);

	lockmgr(&sc->lk, LK_RELEASE);
	return error;
}

/*
 * Interrupt Operation
 */
static
void
ig4iic_intr(void *cookie)
{
	ig4iic_softc_t *sc = cookie;

	lockmgr(&sc->lk, LK_EXCLUSIVE);
/*	reg_write(sc, IG4_REG_INTR_MASK, IG4_INTR_STOP_DET);*/
	reg_read(sc, IG4_REG_CLR_INTR);
	wakeup(sc);
	lockmgr(&sc->lk, LK_RELEASE);
}

DRIVER_MODULE(smbus, ig4iic, smbus_driver, smbus_devclass, NULL, NULL);
