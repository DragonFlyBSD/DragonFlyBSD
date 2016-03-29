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
#define CHV_GPIO_CTL1_INVRX	0x00000010u

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
		    uint16_t pin, int trigger, int polarity, int termination,
		    void *arg, driver_intr_t *handler);
static void	gpio_cherryview_unmap_intr(struct gpio_intel_softc *sc,
		    uint16_t pin);
static int	gpio_cherryview_read_pin(struct gpio_intel_softc *sc,
		    uint16_t pin);
static void	gpio_cherryview_write_pin(struct gpio_intel_softc *sc,
		    uint16_t pin, int value);

static struct gpio_intel_fns gpio_cherryview_fns = {
	.init = gpio_cherryview_init,
	.intr = gpio_cherryview_intr,
	.map_intr = gpio_cherryview_map_intr,
	.unmap_intr = gpio_cherryview_unmap_intr,
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
	bus_write_4(sc->mem_res, CHV_GPIO_REG_MASK, 0);
	bus_write_4(sc->mem_res, CHV_GPIO_REG_IS, 0xffff);
}

static void
gpio_cherryview_intr(void *arg)
{
	struct gpio_intel_softc *sc = (struct gpio_intel_softc *)arg;
	struct pin_intr_map *mapping;
	uint32_t status;
	int i;

	status = bus_read_4(sc->mem_res, CHV_GPIO_REG_IS);
	bus_write_4(sc->mem_res, CHV_GPIO_REG_IS, status);
	for (i = 0; i < 16; i++) {
		if (status & (1 << i)) {
			mapping = &sc->intrmaps[i];
			if (mapping->pin != -1 && mapping->handler != NULL)
				mapping->handler(mapping->arg);
		}
	}
}

/* XXX Add shared/exclusive argument. */
static int
gpio_cherryview_map_intr(struct gpio_intel_softc *sc, uint16_t pin, int trigger,
    int polarity, int termination, void *arg, driver_intr_t *handler)
{
	uint32_t reg, reg1, reg2;
	uint32_t intcfg;
	int i;

	reg1 = bus_read_4(sc->mem_res, PIN_CTL0(pin));
	reg2 = bus_read_4(sc->mem_res, PIN_CTL1(pin));
	device_printf(sc->dev,
	    "pin=%d trigger=%d polarity=%d ctrl0=0x%08x ctrl1=0x%08x\n",
	    pin, trigger, polarity, reg1, reg2);

	intcfg = reg2 & CHV_GPIO_CTL1_INTCFG_MASK;

	/*
	 * Sanity Checks, for now we just abort if the configuration doesn't
	 * match our expectations.
	 */
	if (!(reg1 & CHV_GPIO_CTL0_GPIOEN)) {
		device_printf(sc->dev, "GPIO mode is disabled\n");
		return (ENXIO);
	}
	if ((reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0x0 &&
	    (reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) != 0x200) {
		device_printf(sc->dev, "RX is disabled\n");
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
			if (!(reg2 & CHV_GPIO_CTL1_INVRX)) {
				device_printf(sc->dev,
				    "Invert RX not enabled (needed for "
				    "level/low trigger/polarity)\n");
				return (ENXIO);
			}
		} else {
			if (reg2 & CHV_GPIO_CTL1_INVRX) {
				device_printf(sc->dev,
				    "Invert RX should not be enabled for "
				    "level/high trigger/polarity\n");
				return (ENXIO);
			}
		}
	} else {
		if (polarity == ACPI_ACTIVE_HIGH && intcfg != 2) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 2);
			return (ENXIO);
		} else if (polarity == ACPI_ACTIVE_LOW && intcfg != 1) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 1);
			return (ENXIO);
		} else if (polarity == ACPI_ACTIVE_BOTH && intcfg != 3) {
			device_printf(sc->dev,
			    "Wrong interrupt configuration, is 0x%x should "
			    "be 0x%x\n", intcfg, 3);
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

	/*
	 * XXX Currently we are praying that BIOS/UEFI initialized
	 *     everything correctly.
	 */
	i = (reg1 >> 28) & 0xf;
	if (sc->intrmaps[i].pin != -1) {
		device_printf(sc->dev, "Interrupt line %d already used\n", i);
		return (ENXIO);
	}

	sc->intrmaps[i].pin = pin;
	sc->intrmaps[i].arg = arg;
	sc->intrmaps[i].handler = handler;

	/* unmask interrupt */
	reg = bus_read_4(sc->mem_res, CHV_GPIO_REG_MASK);
	reg |= (1 << i);
	bus_write_4(sc->mem_res, CHV_GPIO_REG_MASK, reg);

	return (0);
}

static void
gpio_cherryview_unmap_intr(struct gpio_intel_softc *sc, uint16_t pin)
{
	uint32_t reg;
	int i;

	for (i = 0; i < 16; i++) {
		if (sc->intrmaps[i].pin == pin) {
			sc->intrmaps[i].pin = -1;
			sc->intrmaps[i].arg = NULL;
			sc->intrmaps[i].handler = NULL;

			/* mask interrupt line */
			reg = bus_read_4(sc->mem_res, CHV_GPIO_REG_MASK);
			reg &= ~(1 << i);
			bus_write_4(sc->mem_res, CHV_GPIO_REG_MASK, reg);
		}
	}
}

static int
gpio_cherryview_read_pin(struct gpio_intel_softc *sc, uint16_t pin)
{
	uint32_t reg;
	int val;

	reg = bus_read_4(sc->mem_res, PIN_CTL0(pin));
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
	uint32_t reg1, reg2;

	reg2 = bus_read_4(sc->mem_res, PIN_CTL1(pin));
	/* Verify that interrupt is disabled */
	KKASSERT((reg2 & CHV_GPIO_CTL1_INTCFG_MASK) == 0);

	reg1 = bus_read_4(sc->mem_res, PIN_CTL0(pin));
	/* Verify that TX is enabled */
	KKASSERT((reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0 ||
	    (reg1 & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x100);

	if (value)
		reg1 |= CHV_GPIO_CTL0_TXSTATE;
	else
		reg1 &= ~CHV_GPIO_CTL0_TXSTATE;
	bus_write_4(sc->mem_res, PIN_CTL0(pin), reg1);
}
