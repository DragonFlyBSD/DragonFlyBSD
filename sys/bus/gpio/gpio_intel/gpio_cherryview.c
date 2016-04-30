/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Imre Vadász <imre@vdsz.com>
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
 * Cherryview GPIO support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <sys/rman.h>

#include "opt_acpi.h"
#include "acpi.h"
#include <dev/acpica/acpivar.h>

#include "gpio_intel_var.h"

#include "gpio_if.h"

#define CHV_GPIO_REG_IS		0x300
#define CHV_GPIO_REG_MASK	0x380
#define CHV_GPIO_REG_PINS	0x4400	/* start of pin control registers */

#define CHV_GPIO_REGOFF_CTL0	0x0
#define CHV_GPIO_REGOFF_CTL1	0x4

#define CHV_GPIO_CTL0_RXSTATE	0x00000001u
#define CHV_GPIO_CTL0_TXSTATE	0x00000002u
#define CHV_GPIO_CTL0_GPIOCFG_MASK 0x00000700u
#define CHV_GPIO_CTL0_GPIOEN	0x00008000u
#define CHV_GPIO_CTL0_PULLUP	0x00800000u
#define CHV_GPIO_CTL1_INTCFG_MASK 0x00000007u
#define CHV_GPIO_CTL1_INVRXDATA	0x00000040u

#define CHV_GPIO_PINSIZE	0x8	/* 8 bytes for each pin */
#define CHV_GPIO_PINCHUNK	15	/* 15 pins at a time */
#define CHV_GPIO_PININC		0x400	/* every 0x400 bytes */

#define PIN_ADDRESS(x)					\
    (CHV_GPIO_REG_PINS +				\
     ((x) / CHV_GPIO_PINCHUNK) * CHV_GPIO_PININC +	\
     ((x) % CHV_GPIO_PINCHUNK) * CHV_GPIO_PINSIZE)

#define PIN_CTL0(x)		(PIN_ADDRESS(x) + CHV_GPIO_REGOFF_CTL0)
#define PIN_CTL1(x)		(PIN_ADDRESS(x) + CHV_GPIO_REGOFF_CTL1)

static void	gpio_cherryview_init(struct gpio_intel_softc *sc);
static void	gpio_cherryview_intr(void *arg);
static int	gpio_cherryview_map_intr(struct gpio_intel_softc *sc,
		    uint16_t pin, int trigger, int polarity, int termination);
static void	gpio_cherryview_unmap_intr(struct gpio_intel_softc *sc,
		    struct pin_intr_map *map);
static void	gpio_cherryview_enable_intr(struct gpio_intel_softc *sc,
		    struct pin_intr_map *map);
static void	gpio_cherryview_disable_intr(struct gpio_intel_softc *sc,
		    struct pin_intr_map *map);
static int	gpio_cherryview_check_io_pin(struct gpio_intel_softc *sc,
		    uint16_t pin, int flags);
static int	gpio_cherryview_read_pin(struct gpio_intel_softc *sc,
		    uint16_t pin);
static void	gpio_cherryview_write_pin(struct gpio_intel_softc *sc,
		    uint16_t pin, int value);

static struct gpio_intel_fns gpio_cherryview_fns = {
	.init = gpio_cherryview_init,
	.intr = gpio_cherryview_intr,
	.map_intr = gpio_cherryview_map_intr,
	.unmap_intr = gpio_cherryview_unmap_intr,
	.enable_intr = gpio_cherryview_enable_intr,
	.disable_intr = gpio_cherryview_disable_intr,
	.check_io_pin = gpio_cherryview_check_io_pin,
	.read_pin = gpio_cherryview_read_pin,
	.write_pin = gpio_cherryview_write_pin,
};

/* _UID=1 */
static struct pinrange chv_sw_ranges[] = {
	{ 0, 7 },
	{ 15, 22 },
	{ 30, 37 },
	{ 45, 52 },
	{ 60, 67 },
	{ 75, 82 },
	{ 90, 97 },
	{ -1, -1 }
};

/* _UID=2 */
static struct pinrange chv_n_ranges[] = {
	{ 0, 8 },
	{ 15, 27 },
	{ 30, 41 },
	{ 45, 56 },
	{ 60, 72 },
	{ -1, -1 }
};

/* _UID=3 */
static struct pinrange chv_e_ranges[] = {
	{ 0, 11 },
	{ 15, 26 },
	{ -1, -1 }
};

/* _UID=4 */
static struct pinrange chv_se_ranges[] = {
	{ 0, 7 },
	{ 15, 26 },
	{ 30, 35 },
	{ 45, 52 },
	{ 60, 69 },
	{ 75, 85 },
	{ -1, -1 }
};

static struct lock gpio_lk;
LOCK_SYSINIT(chvgpiolk, &gpio_lk, "chvgpio", 0);

/*
 * Use global GPIO register lock to workaround erratum:
 *
 * CHT34    Multiple Drivers That Access the GPIO Registers Concurrently May
 *          Result in Unpredictable System Behaviour
 */
static inline uint32_t
chvgpio_read(struct gpio_intel_softc *sc, bus_size_t offset)
{
	uint32_t val;

	lockmgr(&gpio_lk, LK_EXCLUSIVE);
	val = bus_read_4(sc->mem_res, offset);
	lockmgr(&gpio_lk, LK_RELEASE);
	return val;
}

static inline void
chvgpio_write(struct gpio_intel_softc *sc, bus_size_t offset, uint32_t val)
{
	lockmgr(&gpio_lk, LK_EXCLUSIVE);
	bus_write_4(sc->mem_res, offset, val);
	lockmgr(&gpio_lk, LK_RELEASE);
}

int
gpio_cherryview_matchuid(struct gpio_intel_softc *sc)
{
	ACPI_HANDLE handle;

	handle = acpi_get_handle(sc->dev);
	if (acpi_MatchUid(handle, "1")) {
		sc->ranges = chv_sw_ranges;
	} else if (acpi_MatchUid(handle, "2")) {
		sc->ranges = chv_n_ranges;
	} else if (acpi_MatchUid(handle, "3")) {
		sc->ranges = chv_e_ranges;
	} else if (acpi_MatchUid(handle, "4")) {
		sc->ranges = chv_se_ranges;
	} else {
		return (ENXIO);
	}

	sc->fns = &gpio_cherryview_fns;

	return (0);
}

static void
gpio_cherryview_init(struct gpio_intel_softc *sc)
{
	/* mask and clear all interrupt lines */
	chvgpio_write(sc, CHV_GPIO_REG_MASK, 0);
	chvgpio_write(sc, CHV_GPIO_REG_IS, 0xffff);
}

static void
gpio_cherryview_intr(void *arg)
{
	struct gpio_intel_softc *sc = (struct gpio_intel_softc *)arg;
	struct pin_intr_map *mapping;
	uint32_t status;
	int i;

	status = chvgpio_read(sc, CHV_GPIO_REG_IS);
	KKASSERT(NELEM(sc->intrmaps) >= 16);
	for (i = 0; i < 16; i++) {
		if (status & (1U << i)) {
			mapping = &sc->intrmaps[i];
			if (!mapping->is_level) {
				chvgpio_write(sc, CHV_GPIO_REG_IS,
				    (1U << i));
			}
			if (mapping->pin != -1 && mapping->handler != NULL)
				mapping->handler(mapping->arg);
			if (mapping->is_level) {
				chvgpio_write(sc, CHV_GPIO_REG_IS,
				    (1U << i));
			}
		}
	}
}

/* XXX Add shared/exclusive argument. */
static int
gpio_cherryview_map_intr(struct gpio_intel_softc *sc, uint16_t pin, int trigger,
    int polarity, int termination)
{
	uint32_t reg, reg1, reg2;
	uint32_t intcfg, new_intcfg, gpiocfg, new_gpiocfg;
	int i;

	reg1 = chvgpio_read(sc, PIN_CTL0(pin));
	reg2 = chvgpio_read(sc, PIN_CTL1(pin));
	device_printf(sc->dev,
	    "pin=%d trigger=%d polarity=%d ctrl0=0x%08x ctrl1=0x%08x\n",
	    pin, trigger, polarity, reg1, reg2);

	new_intcfg = intcfg = reg2 & CHV_GPIO_CTL1_INTCFG_MASK;
	new_gpiocfg = gpiocfg = reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK;

	/*
	 * Sanity Checks, for now we just abort if the configuration doesn't
	 * match our expectations.
	 */
	if (!(reg1 & CHV_GPIO_CTL0_GPIOEN)) {
		device_printf(sc->dev, "GPIO mode is disabled\n");
		return (ENXIO);
	}
	if (gpiocfg != 0x0 && gpiocfg != 0x200) {
		device_printf(sc->dev, "RX is disabled\n");
		if (gpiocfg == 0x100)
			new_gpiocfg = 0x000;
		else if (gpiocfg == 0x300)
			new_gpiocfg = 0x200;
		else
			return (ENXIO);
	}
	if (trigger == ACPI_LEVEL_SENSITIVE) {
		if (intcfg != 4) {
			device_printf(sc->dev,
			    "trigger is %x, should be 4 (Level)\n", intcfg);
			return (ENXIO);
		}
		if (polarity == ACPI_ACTIVE_BOTH) {
			device_printf(sc->dev,
			    "ACTIVE_BOTH incompatible with level trigger\n");
			return (ENXIO);
		} else if (polarity == ACPI_ACTIVE_LOW) {
			if (!(reg2 & CHV_GPIO_CTL1_INVRXDATA)) {
				device_printf(sc->dev,
				    "Invert RX not enabled (needed for "
				    "level/low trigger/polarity)\n");
				return (ENXIO);
			}
		} else {
			if (reg2 & CHV_GPIO_CTL1_INVRXDATA) {
				device_printf(sc->dev,
				    "Invert RX should not be enabled for "
				    "level/high trigger/polarity\n");
				return (ENXIO);
			}
		}
	} else {
		/*
		 * For edge-triggered interrupts it's definitely harmless to
		 * change between rising-edge, falling-edge and both-edges
		 * triggering.
		 */
		if (polarity == ACPI_ACTIVE_HIGH && intcfg != 2) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 2);
			if (intcfg == 1 || intcfg == 3)
				new_intcfg = 2;
			else
				return (ENXIO);
		} else if (polarity == ACPI_ACTIVE_LOW && intcfg != 1) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 1);
			if (intcfg == 2 || intcfg == 3)
				new_intcfg = 1;
			else
				return (ENXIO);
		} else if (polarity == ACPI_ACTIVE_BOTH && intcfg != 3) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 3);
			if (intcfg == 1 || intcfg == 2)
				new_intcfg = 3;
			else
				return (ENXIO);
		}
	}
	if (termination == ACPI_PIN_CONFIG_PULLUP &&
	    !(reg1 & CHV_GPIO_CTL0_PULLUP)) {
		device_printf(sc->dev,
		    "Wrong termination, is pull-down, should be pull-up\n");
		return (ENXIO);
	} else if (termination == ACPI_PIN_CONFIG_PULLDOWN &&
	    (reg1 & CHV_GPIO_CTL0_PULLUP)) {
		device_printf(sc->dev,
		    "Wrong termination, is pull-up, should be pull-down\n");
		return (ENXIO);
	}

	/* Check if the interrupt/line configured by BIOS/UEFI is unused */
	i = (reg1 >> 28) & 0xf;
	if (sc->intrmaps[i].pin != -1) {
		device_printf(sc->dev, "Interrupt line %d already used\n", i);
		return (ENXIO);
	}

	if (new_intcfg != intcfg) {
		device_printf(sc->dev,
		    "Switching interrupt configuration from 0x%x to 0x%x\n",
		    intcfg, new_intcfg);
		reg = reg2 & ~CHV_GPIO_CTL1_INTCFG_MASK;
		reg |= (new_intcfg & CHV_GPIO_CTL1_INTCFG_MASK) << 0;
		chvgpio_write(sc, PIN_CTL1(pin), reg);
	}

	if (new_gpiocfg != gpiocfg) {
		device_printf(sc->dev,
		    "Switching gpio configuration from 0x%x to 0x%x\n",
		    gpiocfg, new_gpiocfg);
		reg = reg1 & ~CHV_GPIO_CTL0_GPIOCFG_MASK;
		reg |= (new_gpiocfg & CHV_GPIO_CTL0_GPIOCFG_MASK) << 0;
		chvgpio_write(sc, PIN_CTL0(pin), reg);
	}

	sc->intrmaps[i].pin = pin;
	sc->intrmaps[i].intidx = i;
	sc->intrmaps[i].orig_intcfg = intcfg;
	sc->intrmaps[i].orig_gpiocfg = gpiocfg;

	if (trigger == ACPI_LEVEL_SENSITIVE)
		sc->intrmaps[i].is_level = 1;
	else
		sc->intrmaps[i].is_level = 0;

	return (0);
}

static void
gpio_cherryview_unmap_intr(struct gpio_intel_softc *sc,
    struct pin_intr_map *map)
{
	uint32_t reg, intcfg, gpiocfg;
	uint16_t pin = map->pin;

	intcfg = map->orig_intcfg;
	intcfg &= CHV_GPIO_CTL1_INTCFG_MASK;

	gpiocfg = map->orig_gpiocfg;
	gpiocfg &= CHV_GPIO_CTL0_GPIOCFG_MASK;

	map->pin = -1;
	map->intidx = -1;
	map->is_level = 0;
	map->orig_intcfg = 0;
	map->orig_gpiocfg = 0;

	/* Restore interrupt configuration if needed */
	reg = chvgpio_read(sc, PIN_CTL1(pin));
	if ((reg & CHV_GPIO_CTL1_INTCFG_MASK) != intcfg) {
		reg &= ~CHV_GPIO_CTL1_INTCFG_MASK;
		reg |= intcfg;
		chvgpio_write(sc, PIN_CTL1(pin), reg);
	}

	/* Restore gpio configuration if needed */
	reg = chvgpio_read(sc, PIN_CTL0(pin));
	if ((reg & CHV_GPIO_CTL0_GPIOCFG_MASK) != gpiocfg) {
		reg &= ~CHV_GPIO_CTL0_GPIOCFG_MASK;
		reg |= gpiocfg;
		chvgpio_write(sc, PIN_CTL0(pin), reg);
	}
}

static void
gpio_cherryview_enable_intr(struct gpio_intel_softc *sc,
    struct pin_intr_map *map)
{
	uint32_t reg;

	KKASSERT(map->intidx >= 0);

	/* clear interrupt status flag */
	chvgpio_write(sc, CHV_GPIO_REG_IS, (1U << map->intidx));

	/* unmask interrupt */
	reg = chvgpio_read(sc, CHV_GPIO_REG_MASK);
	reg |= (1U << map->intidx);
	chvgpio_write(sc, CHV_GPIO_REG_MASK, reg);
}

static void
gpio_cherryview_disable_intr(struct gpio_intel_softc *sc,
    struct pin_intr_map *map)
{
	uint32_t reg;

	KKASSERT(map->intidx >= 0);

	/* mask interrupt line */
	reg = chvgpio_read(sc, CHV_GPIO_REG_MASK);
	reg &= ~(1U << map->intidx);
	chvgpio_write(sc, CHV_GPIO_REG_MASK, reg);
}

static int
gpio_cherryview_check_io_pin(struct gpio_intel_softc *sc, uint16_t pin,
    int flags)
{
	uint32_t reg1, reg2;

	reg1 = chvgpio_read(sc, PIN_CTL0(pin));
	if (flags & (1U << 0)) {
		/* Verify that RX is enabled */
		if ((reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0 &&
		    (reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0x200) {
			return (0);
		}
	}
	reg2 = chvgpio_read(sc, PIN_CTL1(pin));
	if (flags & (1U << 1)) {
		/* Verify that interrupt is disabled */
		if ((reg2 & CHV_GPIO_CTL1_INTCFG_MASK) != 0)
			return (0);
		/* Verify that TX is enabled */
		if ((reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0 &&
		    (reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0x100) {
			return (0);
		}
	}

	return (1);
}

static int
gpio_cherryview_read_pin(struct gpio_intel_softc *sc, uint16_t pin)
{
	uint32_t reg;
	int val;

	reg = chvgpio_read(sc, PIN_CTL0(pin));
	/* Verify that RX is enabled */
	KKASSERT((reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x0 ||
	    (reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x200);

	if (reg & CHV_GPIO_CTL0_RXSTATE)
		val = 1;
	else
		val = 0;

	return (val);
}

static void
gpio_cherryview_write_pin(struct gpio_intel_softc *sc, uint16_t pin, int value)
{
	uint32_t reg;

	reg = chvgpio_read(sc, PIN_CTL0(pin));
	/* Verify that TX is enabled */
	KKASSERT((reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0 ||
	    (reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x100);

	if (value)
		reg |= CHV_GPIO_CTL0_TXSTATE;
	else
		reg &= ~CHV_GPIO_CTL0_TXSTATE;
	chvgpio_write(sc, PIN_CTL0(pin), reg);
}
