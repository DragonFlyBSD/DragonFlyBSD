/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/mplock2.h>

#include <linux/i2c.h>
#include <linux/export.h>
#include <drm/drmP.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

#include <bus/iicbus/iic.h>
#include <bus/iicbus/iiconf.h>
#include <bus/iicbus/iicbus.h>
#include "iicbus_if.h"
#include "iicbb_if.h"

enum disp_clk {
	CDCLK,
	CZCLK
};

struct gmbus_port {
	const char *name;
	int reg;
};

static const struct gmbus_port gmbus_ports[] = {
	{ "ssc", GPIOB },
	{ "vga", GPIOA },
	{ "panel", GPIOC },
	{ "dpc", GPIOD },
	{ "dpb", GPIOE },
	{ "dpd", GPIOF },
};

/* Intel GPIO access functions */

#define I2C_RISEFALL_TIME 10

static int get_disp_clk_div(struct drm_i915_private *dev_priv,
			    enum disp_clk clk)
{
	u32 reg_val;
	int clk_ratio;

	reg_val = I915_READ(CZCLK_CDCLK_FREQ_RATIO);

	if (clk == CDCLK)
		clk_ratio =
			((reg_val & CDCLK_FREQ_MASK) >> CDCLK_FREQ_SHIFT) + 1;
	else
		clk_ratio = (reg_val & CZCLK_FREQ_MASK) + 1;

	return clk_ratio;
}

static void gmbus_set_freq(struct drm_i915_private *dev_priv)
{
	int vco, gmbus_freq = 0, cdclk_div;

	BUG_ON(!IS_VALLEYVIEW(dev_priv->dev));

	vco = valleyview_get_vco(dev_priv);

	/* Get the CDCLK divide ratio */
	cdclk_div = get_disp_clk_div(dev_priv, CDCLK);

	/*
	 * Program the gmbus_freq based on the cdclk frequency.
	 * BSpec erroneously claims we should aim for 4MHz, but
	 * in fact 1MHz is the correct frequency.
	 */
	if (cdclk_div)
		gmbus_freq = (vco << 1) / cdclk_div;

	if (WARN_ON(gmbus_freq == 0))
		return;

	I915_WRITE(GMBUSFREQ_VLV, gmbus_freq);
}

void
intel_i2c_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/*
	 * In BIOS-less system, program the correct gmbus frequency
	 * before reading edid.
	 */
	if (IS_VALLEYVIEW(dev))
		gmbus_set_freq(dev_priv);

	I915_WRITE(dev_priv->gpio_mmio_base + GMBUS0, 0);
	I915_WRITE(dev_priv->gpio_mmio_base + GMBUS4, 0);
}

static void intel_i2c_quirk_set(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	/* When using bit bashing for I2C, this bit needs to be set to 1 */
	if (!IS_PINEVIEW(dev_priv->dev))
		return;

	val = I915_READ(DSPCLK_GATE_D);
	if (enable)
		val |= DPCUNIT_CLOCK_GATE_DISABLE;
	else
		val &= ~DPCUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, val);
}

static u32 get_reserved(device_t idev)
{
	struct intel_iic_softc *sc = device_get_softc(idev);
	struct drm_device *dev = sc->drm_dev;
	struct drm_i915_private *dev_priv;
	u32 reserved = 0;

	dev_priv = dev->dev_private;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(dev) && !IS_845G(dev))
		reserved = I915_READ_NOTRACE(sc->reg) &
					     (GPIO_DATA_PULLUP_DISABLE |
					      GPIO_CLOCK_PULLUP_DISABLE);

	return reserved;
}

static int get_clock(device_t idev)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	u32 reserved;

	sc = device_get_softc(idev);
	dev_priv = sc->drm_dev->dev_private;

	reserved = get_reserved(idev);

	I915_WRITE_NOTRACE(sc->reg, reserved | GPIO_CLOCK_DIR_MASK);
	I915_WRITE_NOTRACE(sc->reg, reserved);
	return ((I915_READ_NOTRACE(sc->reg) & GPIO_CLOCK_VAL_IN) != 0);
}

static int get_data(device_t idev)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	u32 reserved;

	sc = device_get_softc(idev);
	dev_priv = sc->drm_dev->dev_private;

	reserved = get_reserved(idev);

	I915_WRITE_NOTRACE(sc->reg, reserved | GPIO_DATA_DIR_MASK);
	I915_WRITE_NOTRACE(sc->reg, reserved);
	return ((I915_READ_NOTRACE(sc->reg) & GPIO_DATA_VAL_IN) != 0);
}

static int
intel_iicbus_reset(device_t idev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct intel_iic_softc *sc;
	struct drm_device *dev;

	sc = device_get_softc(idev);
	dev = sc->drm_dev;

	intel_i2c_reset(dev);
	return (0);
}

static void set_clock(device_t idev, int val)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	u32 clock_bits, reserved;

	sc = device_get_softc(idev);
	dev_priv = sc->drm_dev->dev_private;

	reserved = get_reserved(idev);
	if (val)
		clock_bits = GPIO_CLOCK_DIR_IN | GPIO_CLOCK_DIR_MASK;
	else
		clock_bits = GPIO_CLOCK_DIR_OUT | GPIO_CLOCK_DIR_MASK |
		    GPIO_CLOCK_VAL_MASK;

	I915_WRITE_NOTRACE(sc->reg, reserved | clock_bits);
	POSTING_READ(sc->reg);
}

static void set_data(device_t idev, int val)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	u32 reserved;
	u32 data_bits;

	sc = device_get_softc(idev);
	dev_priv = sc->drm_dev->dev_private;

	reserved = get_reserved(idev);
	if (val)
		data_bits = GPIO_DATA_DIR_IN | GPIO_DATA_DIR_MASK;
	else
		data_bits = GPIO_DATA_DIR_OUT | GPIO_DATA_DIR_MASK |
		    GPIO_DATA_VAL_MASK;

	I915_WRITE_NOTRACE(sc->reg, reserved | data_bits);
	POSTING_READ(sc->reg);
}

static const char *gpio_names[GMBUS_NUM_PORTS] = {
	"ssc",
	"vga",
	"panel",
	"dpc",
	"dpb",
	"dpd",
};

static int
intel_gpio_setup(device_t idev)
{
	static const int map_pin_to_reg[] = {
		0,
		GPIOB,
		GPIOA,
		GPIOC,
		GPIOD,
		GPIOE,
		GPIOF,
		0
	};

	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	int pin;

	sc = device_get_softc(idev);
	sc->drm_dev = device_get_softc(device_get_parent(idev));
	dev_priv = sc->drm_dev->dev_private;
	pin = device_get_unit(idev);

	ksnprintf(sc->name, sizeof(sc->name), "i915 iicbb %s", gpio_names[pin]);
	device_set_desc(idev, sc->name);

	sc->reg0 = (pin + 1) | GMBUS_RATE_100KHZ;
	sc->reg = map_pin_to_reg[pin + 1];
	if (HAS_PCH_SPLIT(dev_priv->dev))
		sc->reg += PCH_GPIOA - GPIOA;

	/* add generic bit-banging code */
	sc->iic_dev = device_add_child(idev, "iicbb", -1);
	if (sc->iic_dev == NULL)
		return (ENXIO);
	device_quiet(sc->iic_dev);
	bus_generic_attach(idev);

	return (0);
}

static int
intel_i2c_quirk_xfer(device_t idev, struct iic_msg *msgs, int nmsgs)
{
	device_t bridge_dev;
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	int ret;
	int i;

	bridge_dev = device_get_parent(device_get_parent(idev));
	sc = device_get_softc(bridge_dev);
	dev_priv = sc->drm_dev->dev_private;

	intel_i2c_reset(sc->drm_dev);
	intel_i2c_quirk_set(dev_priv, true);
	IICBB_SETSDA(bridge_dev, 1);
	IICBB_SETSCL(bridge_dev, 1);
	DELAY(I2C_RISEFALL_TIME);

	for (i = 0; i < nmsgs - 1; i++) {
		/* force use of repeated start instead of default stop+start */
		msgs[i].flags |= IIC_M_NOSTOP;
	}
	ret = iicbus_transfer(idev, msgs, nmsgs);
	IICBB_SETSDA(bridge_dev, 1);
	IICBB_SETSCL(bridge_dev, 1);
	intel_i2c_quirk_set(dev_priv, false);

	return (ret);
}

static int
gmbus_wait_hw_status(struct drm_i915_private *dev_priv,
		     u32 gmbus2_status,
		     u32 gmbus4_irq_en)
{
	int i;
	int reg_offset = dev_priv->gpio_mmio_base;
	u32 gmbus2 = 0;
	DEFINE_WAIT(wait);

	if (!HAS_GMBUS_IRQ(dev_priv->dev))
		gmbus4_irq_en = 0;

	/* Important: The hw handles only the first bit, so set only one! Since
	 * we also need to check for NAKs besides the hw ready/idle signal, we
	 * need to wake up periodically and check that ourselves. */
	I915_WRITE(GMBUS4 + reg_offset, gmbus4_irq_en);

	for (i = 0; i < msecs_to_jiffies_timeout(50); i++) {
		prepare_to_wait(&dev_priv->gmbus_wait_queue, &wait,
				TASK_UNINTERRUPTIBLE);

		gmbus2 = I915_READ_NOTRACE(GMBUS2 + reg_offset);
		if (gmbus2 & (GMBUS_SATOER | gmbus2_status))
			break;

		schedule_timeout(1);
	}
	finish_wait(&dev_priv->gmbus_wait_queue, &wait);

	I915_WRITE(GMBUS4 + reg_offset, 0);

	if (gmbus2 & GMBUS_SATOER)
		return -ENXIO;
	if (gmbus2 & gmbus2_status)
		return 0;
	return -ETIMEDOUT;
}

static int
gmbus_wait_idle(struct drm_i915_private *dev_priv)
{
	int ret;
	int reg_offset = dev_priv->gpio_mmio_base;

#define C ((I915_READ_NOTRACE(GMBUS2 + reg_offset) & GMBUS_ACTIVE) == 0)

	if (!HAS_GMBUS_IRQ(dev_priv->dev))
		return wait_for(C, 10);

	/* Important: The hw handles only the first bit, so set only one! */
	I915_WRITE(GMBUS4 + reg_offset, GMBUS_IDLE_EN);

	ret = wait_event_timeout(dev_priv->gmbus_wait_queue, C,
				 msecs_to_jiffies_timeout(10));

	I915_WRITE(GMBUS4 + reg_offset, 0);

	if (ret)
		return 0;
	else
		return -ETIMEDOUT;
#undef C
}

static int
gmbus_xfer_read(struct drm_i915_private *dev_priv, struct i2c_msg *msg,
		u32 gmbus1_index)
{
	int reg_offset = dev_priv->gpio_mmio_base;
	u16 len = msg->len;
	u8 *buf = msg->buf;

	I915_WRITE(GMBUS1 + reg_offset,
		   gmbus1_index |
		   GMBUS_CYCLE_WAIT |
		   (len << GMBUS_BYTE_COUNT_SHIFT) |
		   (msg->slave << (GMBUS_SLAVE_ADDR_SHIFT - 1)) |
		   GMBUS_SLAVE_READ | GMBUS_SW_RDY);
	while (len) {
		int ret;
		u32 val, loop = 0;

		ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_RDY,
					   GMBUS_HW_RDY_EN);
		if (ret)
			return ret;

		val = I915_READ(GMBUS3 + reg_offset);
		do {
			*buf++ = val & 0xff;
			val >>= 8;
		} while (--len && ++loop < 4);
	}

	return 0;
}

static int
gmbus_xfer_write(struct drm_i915_private *dev_priv, struct i2c_msg *msg)
{
	int reg_offset = dev_priv->gpio_mmio_base;
	u16 len = msg->len;
	u8 *buf = msg->buf;
	u32 val, loop;

	val = loop = 0;
	while (len && loop < 4) {
		val |= *buf++ << (8 * loop++);
		len -= 1;
	}

	I915_WRITE(GMBUS3 + reg_offset, val);
	I915_WRITE(GMBUS1 + reg_offset,
		   GMBUS_CYCLE_WAIT |
		   (msg->len << GMBUS_BYTE_COUNT_SHIFT) |
		   (msg->slave << (GMBUS_SLAVE_ADDR_SHIFT - 1)) |
		   GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
	while (len) {
		int ret;

		val = loop = 0;
		do {
			val |= *buf++ << (8 * loop);
		} while (--len && ++loop < 4);

		I915_WRITE(GMBUS3 + reg_offset, val);

		ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_RDY,
					   GMBUS_HW_RDY_EN);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * The gmbus controller can combine a 1 or 2 byte write with a read that
 * immediately follows it by using an "INDEX" cycle.
 */
static bool
gmbus_is_index_read(struct i2c_msg *msgs, int i, int num)
{
	return (i + 1 < num &&
		!(msgs[i].flags & I2C_M_RD) && msgs[i].len <= 2 &&
		(msgs[i + 1].flags & I2C_M_RD));
}

static int
gmbus_xfer_index_read(struct drm_i915_private *dev_priv, struct i2c_msg *msgs)
{
	int reg_offset = dev_priv->gpio_mmio_base;
	u32 gmbus1_index = 0;
	u32 gmbus5 = 0;
	int ret;

	if (msgs[0].len == 2)
		gmbus5 = GMBUS_2BYTE_INDEX_EN |
			 msgs[0].buf[1] | (msgs[0].buf[0] << 8);
	if (msgs[0].len == 1)
		gmbus1_index = GMBUS_CYCLE_INDEX |
			       (msgs[0].buf[0] << GMBUS_SLAVE_INDEX_SHIFT);

	/* GMBUS5 holds 16-bit index */
	if (gmbus5)
		I915_WRITE(GMBUS5 + reg_offset, gmbus5);

	ret = gmbus_xfer_read(dev_priv, &msgs[1], gmbus1_index);

	/* Clear GMBUS5 after each index transfer */
	if (gmbus5)
		I915_WRITE(GMBUS5 + reg_offset, 0);

	return ret;
}

static int
gmbus_xfer(struct device *adapter,
	   struct i2c_msg *msgs,
	   int num)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	int i, reg_offset, unit;
	int ret = 0;

	sc = device_get_softc(adapter);
	dev_priv = sc->drm_dev->dev_private;
	unit = device_get_unit(adapter);

	mutex_lock(&dev_priv->gmbus_mutex);

	if (sc->force_bit_dev) {
		ret = intel_i2c_quirk_xfer(dev_priv->bbbus[unit], msgs, num);
		goto out;
	}

	reg_offset = HAS_PCH_SPLIT(dev_priv->dev) ? PCH_GMBUS0 - GMBUS0 : 0;

	I915_WRITE(GMBUS0 + reg_offset, sc->reg0);

	for (i = 0; i < num; i++) {
		if (gmbus_is_index_read(msgs, i, num)) {
			ret = gmbus_xfer_index_read(dev_priv, &msgs[i]);
			i += 1;  /* set i to the index of the read xfer */
		} else if (msgs[i].flags & I2C_M_RD) {
			ret = gmbus_xfer_read(dev_priv, &msgs[i], 0);
		} else {
			ret = gmbus_xfer_write(dev_priv, &msgs[i]);
		}

		if (ret == -ETIMEDOUT)
			goto timeout;
		if (ret == -ENXIO)
			goto clear_err;

		ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_WAIT_PHASE,
					   GMBUS_HW_WAIT_EN);
		if (ret == -ENXIO)
			goto clear_err;
		if (ret)
			goto timeout;
	}

	/* Generate a STOP condition on the bus. Note that gmbus can't generata
	 * a STOP on the very first cycle. To simplify the code we
	 * unconditionally generate the STOP condition with an additional gmbus
	 * cycle. */
	I915_WRITE(GMBUS1 + reg_offset, GMBUS_CYCLE_STOP | GMBUS_SW_RDY);

	/* Mark the GMBUS interface as disabled after waiting for idle.
	 * We will re-enable it at the start of the next xfer,
	 * till then let it sleep.
	 */
	if (gmbus_wait_idle(dev_priv)) {
		DRM_DEBUG_KMS("GMBUS [%s] timed out waiting for idle\n",
			 sc->name);
		ret = -ETIMEDOUT;
	}
	I915_WRITE(GMBUS0 + reg_offset, 0);
	ret = ret ?: i;
	goto timeout;	/* XXX: should be out */

clear_err:
	/*
	 * Wait for bus to IDLE before clearing NAK.
	 * If we clear the NAK while bus is still active, then it will stay
	 * active and the next transaction may fail.
	 *
	 * If no ACK is received during the address phase of a transaction, the
	 * adapter must report -ENXIO. It is not clear what to return if no ACK
	 * is received at other times. But we have to be careful to not return
	 * spurious -ENXIO because that will prevent i2c and drm edid functions
	 * from retrying. So return -ENXIO only when gmbus properly quiescents -
	 * timing out seems to happen when there _is_ a ddc chip present, but
	 * it's slow responding and only answers on the 2nd retry.
	 */
	ret = -ENXIO;
	if (gmbus_wait_idle(dev_priv)) {
		DRM_DEBUG_KMS("GMBUS [%s] timed out after NAK\n",
			      sc->name);
		ret = -ETIMEDOUT;
	}

	/* Toggle the Software Clear Interrupt bit. This has the effect
	 * of resetting the GMBUS controller and so clearing the
	 * BUS_ERROR raised by the slave's NAK.
	 */
	I915_WRITE(GMBUS1 + reg_offset, GMBUS_SW_CLR_INT);
	I915_WRITE(GMBUS1 + reg_offset, 0);
	I915_WRITE(GMBUS0 + reg_offset, 0);

	DRM_DEBUG_KMS("GMBUS [%s] NAK for addr: %04x %c(%d)\n",
			 sc->name, msgs[i].slave,
			 (msgs[i].flags & I2C_M_RD) ? 'r' : 'w', msgs[i].len);

	goto out;

timeout:
	DRM_INFO("GMBUS [%s] timed out, falling back to bit banging on pin %d\n",
		 sc->name, sc->reg0 & 0xff);
	I915_WRITE(GMBUS0 + reg_offset, 0);

	/* Hardware may not support GMBUS over these pins? Try GPIO bitbanging instead. */
	sc->force_bit_dev = true;
	ret = intel_i2c_quirk_xfer(dev_priv->bbbus[unit], msgs, num);

out:
	mutex_unlock(&dev_priv->gmbus_mutex);
	return ret;
}

struct device *intel_gmbus_get_adapter(struct drm_i915_private *dev_priv,
					    unsigned port)
{
	WARN_ON(!intel_gmbus_is_port_valid(port));
	/* -1 to map pin pair to gmbus index */
	return (intel_gmbus_is_port_valid(port)) ?
		dev_priv->gmbus[port-1] : NULL;
}

void
intel_gmbus_set_speed(device_t idev, int speed)
{
	struct intel_iic_softc *sc;

	sc = device_get_softc(device_get_parent(idev));

	sc->reg0 = (sc->reg0 & ~(0x3 << 8)) | speed;
}

void
intel_gmbus_force_bit(device_t idev, bool force_bit)
{
	struct intel_iic_softc *sc;

	sc = device_get_softc(device_get_parent(idev));
	sc->force_bit_dev += force_bit ? 1 : -1;
	DRM_DEBUG_KMS("%sabling bit-banging on %s. force bit now %d\n",
		      force_bit ? "en" : "dis", sc->name,
		      sc->force_bit_dev);
}

static int
intel_gmbus_probe(device_t dev)
{

	return (BUS_PROBE_SPECIFIC);
}

static int
intel_gmbus_attach(device_t idev)
{
	struct drm_i915_private *dev_priv;
	struct intel_iic_softc *sc;
	int pin;

	sc = device_get_softc(idev);
	sc->drm_dev = device_get_softc(device_get_parent(idev));
	dev_priv = sc->drm_dev->dev_private;
	pin = device_get_unit(idev);

	ksnprintf(sc->name, sizeof(sc->name), "gmbus bus %s", gpio_names[pin]);
	device_set_desc(idev, sc->name);

	/* By default use a conservative clock rate */
	sc->reg0 = (pin + 1) | GMBUS_RATE_100KHZ;

	/* XXX force bit banging until GMBUS is fully debugged */
	if (IS_GEN2(sc->drm_dev)) {
		sc->force_bit_dev = true;
	}

	/* add bus interface device */
	sc->iic_dev = device_add_child(idev, "iicbus", -1);
	if (sc->iic_dev == NULL)
		return (ENXIO);
	device_quiet(sc->iic_dev);
	bus_generic_attach(idev);

	return (0);
}

static int
intel_gmbus_detach(device_t idev)
{
	struct intel_iic_softc *sc;
	struct drm_i915_private *dev_priv;
	device_t child;
	int u;

	sc = device_get_softc(idev);
	u = device_get_unit(idev);
	dev_priv = sc->drm_dev->dev_private;

	child = sc->iic_dev;
	bus_generic_detach(idev);
	if (child != NULL)
		device_delete_child(idev, child);

	return (0);
}

static int
intel_iicbb_probe(device_t dev)
{

	return (BUS_PROBE_DEFAULT);
}

static int
intel_iicbb_detach(device_t idev)
{
	struct intel_iic_softc *sc;
	device_t child;

	sc = device_get_softc(idev);
	child = sc->iic_dev;
	bus_generic_detach(idev);
	if (child)
		device_delete_child(idev, child);
	return (0);
}

static device_method_t intel_gmbus_methods[] = {
	DEVMETHOD(device_probe,		intel_gmbus_probe),
	DEVMETHOD(device_attach,	intel_gmbus_attach),
	DEVMETHOD(device_detach,	intel_gmbus_detach),
	DEVMETHOD(iicbus_reset,		intel_iicbus_reset),
	DEVMETHOD(iicbus_transfer,	gmbus_xfer),
	DEVMETHOD_END
};
static driver_t intel_gmbus_driver = {
	"intel_gmbus",
	intel_gmbus_methods,
	sizeof(struct intel_iic_softc)
};
static devclass_t intel_gmbus_devclass;
DRIVER_MODULE_ORDERED(intel_gmbus, drm, intel_gmbus_driver,
    intel_gmbus_devclass, 0, 0, SI_ORDER_FIRST);
DRIVER_MODULE(iicbus, intel_gmbus, iicbus_driver, iicbus_devclass, NULL, NULL);

static device_method_t intel_iicbb_methods[] =	{
	DEVMETHOD(device_probe,		intel_iicbb_probe),
	DEVMETHOD(device_attach,	intel_gpio_setup),
	DEVMETHOD(device_detach,	intel_iicbb_detach),

	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_print_child,	bus_generic_print_child),

	DEVMETHOD(iicbb_callback,	iicbus_null_callback),
	DEVMETHOD(iicbb_reset,		intel_iicbus_reset),
	DEVMETHOD(iicbb_setsda,		set_data),
	DEVMETHOD(iicbb_setscl,		set_clock),
	DEVMETHOD(iicbb_getsda,		get_data),
	DEVMETHOD(iicbb_getscl,		get_clock),
	DEVMETHOD_END
};
static driver_t intel_iicbb_driver = {
	"intel_iicbb",
	intel_iicbb_methods,
	sizeof(struct intel_iic_softc)
};
static devclass_t intel_iicbb_devclass;
DRIVER_MODULE_ORDERED(intel_iicbb, drm, intel_iicbb_driver,
    intel_iicbb_devclass, 0, 0, SI_ORDER_FIRST);
DRIVER_MODULE(iicbb, intel_iicbb, iicbb_driver, iicbb_devclass, NULL, NULL);

static void intel_teardown_gmbus_m(struct drm_device *dev, int m);

int
intel_setup_gmbus(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	device_t iic_dev;
	int i, ret;

	if (HAS_PCH_NOP(dev))
		return 0;
	else if (HAS_PCH_SPLIT(dev))
		dev_priv->gpio_mmio_base = PCH_GPIOA - GPIOA;
	else if (IS_VALLEYVIEW(dev))
		dev_priv->gpio_mmio_base = VLV_DISPLAY_BASE;
	else
		dev_priv->gpio_mmio_base = 0;

	lockinit(&dev_priv->gmbus_mutex, "gmbus", 0, LK_CANRECURSE);
	init_waitqueue_head(&dev_priv->gmbus_wait_queue);

	dev_priv->gmbus_bridge = kmalloc(sizeof(device_t) * GMBUS_NUM_PORTS,
	    M_DRM, M_WAITOK | M_ZERO);
	dev_priv->bbbus_bridge = kmalloc(sizeof(device_t) * GMBUS_NUM_PORTS,
	    M_DRM, M_WAITOK | M_ZERO);
	dev_priv->gmbus = kmalloc(sizeof(device_t) * GMBUS_NUM_PORTS,
	    M_DRM, M_WAITOK | M_ZERO);
	dev_priv->bbbus = kmalloc(sizeof(device_t) * GMBUS_NUM_PORTS,
	    M_DRM, M_WAITOK | M_ZERO);

	for (i = 0; i < GMBUS_NUM_PORTS; i++) {
		/*
		 * Initialized bbbus_bridge before gmbus_bridge, since
		 * gmbus may decide to force quirk transfer in the
		 * attachment code.
		 */
		dev_priv->bbbus_bridge[i] = device_add_child(dev->dev,
		    "intel_iicbb", i);
		if (dev_priv->bbbus_bridge[i] == NULL) {
			DRM_ERROR("bbbus bridge %d creation failed\n", i);
			ret = ENXIO;
			goto err;
		}
		device_quiet(dev_priv->bbbus_bridge[i]);
		ret = device_probe_and_attach(dev_priv->bbbus_bridge[i]);
		if (ret != 0) {
			DRM_ERROR("bbbus bridge %d attach failed, %d\n", i,
			    ret);
			goto err;
		}

		iic_dev = device_find_child(dev_priv->bbbus_bridge[i], "iicbb",
		    -1);
		if (iic_dev == NULL) {
			DRM_ERROR("bbbus bridge doesn't have iicbb child\n");
			goto err;
		}
		iic_dev = device_find_child(iic_dev, "iicbus", -1);
		if (iic_dev == NULL) {
			DRM_ERROR(
		"bbbus bridge doesn't have iicbus grandchild\n");
			goto err;
		}

		dev_priv->bbbus[i] = iic_dev;

		dev_priv->gmbus_bridge[i] = device_add_child(dev->dev,
		    "intel_gmbus", i);
		if (dev_priv->gmbus_bridge[i] == NULL) {
			DRM_ERROR("gmbus bridge %d creation failed\n", i);
			ret = ENXIO;
			goto err;
		}
		device_quiet(dev_priv->gmbus_bridge[i]);
		ret = device_probe_and_attach(dev_priv->gmbus_bridge[i]);
		if (ret != 0) {
			DRM_ERROR("gmbus bridge %d attach failed, %d\n", i,
			    ret);
			ret = ENXIO;
			goto err;
		}

		iic_dev = device_find_child(dev_priv->gmbus_bridge[i],
		    "iicbus", -1);
		if (iic_dev == NULL) {
			DRM_ERROR("gmbus bridge doesn't have iicbus child\n");
			goto err;
		}
		dev_priv->gmbus[i] = iic_dev;

		intel_i2c_reset(dev);
	}

	return (0);

err:
	intel_teardown_gmbus_m(dev, i);
	return (ret);
}

static void
intel_teardown_gmbus_m(struct drm_device *dev, int m)
{
	struct drm_i915_private *dev_priv;

	dev_priv = dev->dev_private;

	drm_free(dev_priv->gmbus, M_DRM);
	dev_priv->gmbus = NULL;
	drm_free(dev_priv->bbbus, M_DRM);
	dev_priv->bbbus = NULL;
	drm_free(dev_priv->gmbus_bridge, M_DRM);
	dev_priv->gmbus_bridge = NULL;
	drm_free(dev_priv->bbbus_bridge, M_DRM);
	dev_priv->bbbus_bridge = NULL;
	lockuninit(&dev_priv->gmbus_mutex);
}

void
intel_teardown_gmbus(struct drm_device *dev)
{

	get_mplock();
	intel_teardown_gmbus_m(dev, GMBUS_NUM_PORTS);
	rel_mplock();
}
