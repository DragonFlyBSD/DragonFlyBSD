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
 * Intel SoC gpio driver.
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

#include "acpi.h"
#include "opt_acpi.h"
#include <dev/acpica/acpivar.h>

#include <bus/pci/pcivar.h>

#include <bus/gpio/gpio_acpi/gpio_acpivar.h>

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

struct pinrange {
	int start;
	int end;
};

struct pin_intr_map {
	int pin;
	void *arg;
	driver_intr_t *handler;
};

struct gpio_intel_softc {
	device_t dev;
	struct resource *mem_res;
	struct resource *irq_res;
	void		*intrhand;
	struct lock	lk;
	struct pinrange *ranges;
	struct pin_intr_map intrmaps[16];
	void		*acpireg;
};

static int	gpio_intel_probe(device_t dev);
static int	gpio_intel_attach(device_t dev);
static int	gpio_intel_detach(device_t dev);
static int	gpio_intel_alloc_intr(device_t dev, u_int pin, int trigger,
		    int polarity, int termination, void *arg,
		    driver_intr_t *handler);
static int	gpio_intel_free_intr(device_t dev, u_int pin);
static int	chv_gpio_read_pin(device_t dev, u_int pin);
static void	chv_gpio_write_pin(device_t dev, u_int pin, int value);

static void	chv_gpio_intr(void *arg);
static int	chv_gpio_map_intr(struct gpio_intel_softc *sc, uint16_t pin,
		    int trigger, int polarity, int termination, void *arg,
		    driver_intr_t *handler);
static void	chv_gpio_unmap_intr(struct gpio_intel_softc *sc, uint16_t pin);
static BOOLEAN	gpio_intel_pin_exists(struct gpio_intel_softc *sc,
		    uint16_t pin);

static BOOLEAN	acpi_MatchUid(ACPI_HANDLE h, const char *uid)
{
	ACPI_DEVICE_INFO	*devinfo;
	int			ret;

	ret = FALSE;
	if (uid == NULL || h == NULL ||
	    ACPI_FAILURE(AcpiGetObjectInfo(h, &devinfo)))
		return (ret);

	if ((devinfo->Valid & ACPI_VALID_UID) != 0 &&
	    strcmp(uid, devinfo->UniqueId.String) == 0)
		ret = TRUE;

	AcpiOsFree(devinfo);
	return (ret);
}

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

static int
gpio_intel_probe(device_t dev)
{
        static char *chvgpio_ids[] = { "INT33FF", NULL };

        if (acpi_disabled("gpio_intel") ||
            ACPI_ID_PROBE(device_get_parent(dev), dev, chvgpio_ids) == NULL)
                return (ENXIO);

	device_set_desc(dev, "Intel Cherry Trail GPIO Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
gpio_intel_attach(device_t dev)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	ACPI_HANDLE handle;
	int error, i, rid;

	lockinit(&sc->lk, "gpio_intel", 0, LK_CANRECURSE);

	sc->dev = dev;

	handle = acpi_get_handle(dev);
	if (acpi_MatchUid(handle, "1")) {
		sc->ranges = chv_sw_ranges;
	} else if (acpi_MatchUid(handle, "2")) {
		sc->ranges = chv_n_ranges;
	} else if (acpi_MatchUid(handle, "3")) {
		sc->ranges = chv_e_ranges;
	} else if (acpi_MatchUid(handle, "4")) {
		sc->ranges = chv_se_ranges;
	} else {
		error = ENXIO;
		goto err;
	}

	for (i = 0; i < 16; i++) {
		sc->intrmaps[i].pin = -1;
	}

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "unable to map registers");
		error = ENXIO;
		goto err;
	}
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "unable to map interrupt");
		error = ENXIO;
		goto err;
	}

	/* Activate the interrupt */
	error = bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE,
	    chv_gpio_intr, sc, &sc->intrhand, NULL);
	if (error)
		device_printf(dev, "Can't setup IRQ\n");

	/* power up the controller */
	pci_set_powerstate(dev, PCI_POWERSTATE_D0);

	/* mask and clear all interrupt lines */
	bus_write_4(sc->mem_res, CHV_GPIO_REG_MASK, 0);
	bus_write_4(sc->mem_res, CHV_GPIO_REG_IS, 0xffff);

	sc->acpireg = gpio_acpi_register(dev);

	return (0);

err:
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		sc->irq_res = NULL;
	}
	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
		sc->mem_res = NULL;
	}
	return (error);
}

static int
gpio_intel_detach(device_t dev)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);

	gpio_acpi_unregister(dev, sc->acpireg);

	bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		sc->irq_res = NULL;
	}
	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
		sc->mem_res = NULL;
	}
	lockuninit(&sc->lk);

	pci_set_powerstate(dev, PCI_POWERSTATE_D3);

	return 0;
}

/*
 * The trigger, polarity and termination parameters are only used for
 * sanity checking. The gpios should already be configured correctly by
 * the firmware.
 */
static int
gpio_intel_alloc_intr(device_t dev, u_int pin, int trigger, int polarity,
    int termination, void *arg, driver_intr_t *handler)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	int ret;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	if (gpio_intel_pin_exists(sc, pin)) {
		ret = chv_gpio_map_intr(sc, pin, trigger, polarity,
		    termination, arg, handler);
	} else {
		device_printf(sc->dev, "Invalid pin number %d\n", pin);
		ret = ENOENT;
	}

	lockmgr(&sc->lk, LK_RELEASE);

	return (ret);
}

static int
gpio_intel_free_intr(device_t dev, u_int pin)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	int ret;

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	if (gpio_intel_pin_exists(sc, pin)) {
		chv_gpio_unmap_intr(sc, pin);
		ret = 0;
	} else {
		device_printf(sc->dev, "Invalid pin number %d\n", pin);
		ret = ENOENT;
	}

	lockmgr(&sc->lk, LK_RELEASE);

	return (ret);
}

static void
chv_gpio_intr(void *arg)
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
chv_gpio_map_intr(struct gpio_intel_softc *sc, uint16_t pin, int trigger,
    int polarity, int termination, void *arg, driver_intr_t *handler)
{
	uint32_t reg, reg1, reg2;
	uint32_t intcfg;
	int i;

	/* Make sure this pin isn't mapped yet */
	for (i = 0; i < 16; i++) {
		if (sc->intrmaps[i].pin == pin)
			return (ENOMEM);
	}

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
chv_gpio_unmap_intr(struct gpio_intel_softc *sc, uint16_t pin)
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

static BOOLEAN
gpio_intel_pin_exists(struct gpio_intel_softc *sc, uint16_t pin)
{
	struct pinrange *r;

	for (r = sc->ranges; r->start != -1 && r->end != -1; r++) {
		if (r->start <= (int)pin && r->end >= (int)pin)
			return (TRUE);
	}

	return (FALSE);
}

static int
chv_gpio_read_pin(device_t dev, u_int pin)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	uint32_t reg;
	int val;

	/* This operation mustn't fail, otherwise ACPI would be in trouble */
	KKASSERT(gpio_intel_pin_exists(sc, pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	reg = bus_read_4(sc->mem_res, PIN_CTL0(pin));
	/* Verify that RX is enabled */
	KKASSERT((reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x0 ||
	    (reg & CHV_GPIO_CTL0_GPIOCFG_MASK) == 0x200);

	if (reg & CHV_GPIO_CTL0_RXSTATE)
		val = 1;
	else
		val = 0;

	lockmgr(&sc->lk, LK_RELEASE);

	return (val);
}

static void
chv_gpio_write_pin(device_t dev, u_int pin, int value)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	uint32_t reg1, reg2;

	/* This operation mustn't fail, otherwise ACPI would be in trouble */
	KKASSERT(gpio_intel_pin_exists(sc, pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);

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

	lockmgr(&sc->lk, LK_RELEASE);
}

static device_method_t gpio_intel_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, gpio_intel_probe),
	DEVMETHOD(device_attach, gpio_intel_attach),
	DEVMETHOD(device_detach, gpio_intel_detach),

	/* GPIO methods */
	DEVMETHOD(gpio_alloc_intr, gpio_intel_alloc_intr),
	DEVMETHOD(gpio_free_intr, gpio_intel_free_intr),
	DEVMETHOD(gpio_read_pin, chv_gpio_read_pin),
	DEVMETHOD(gpio_write_pin, chv_gpio_write_pin),

	DEVMETHOD_END
};

static driver_t gpio_intel_driver = {
        "gpio_intel",
        gpio_intel_methods,
        sizeof(struct gpio_intel_softc)
};

static devclass_t gpio_intel_devclass;

DRIVER_MODULE(gpio_intel, acpi, gpio_intel_driver, gpio_intel_devclass,
    NULL, NULL);
MODULE_DEPEND(gpio_intel, acpi, 1, 1, 1);
MODULE_DEPEND(gpio_intel, gpio_acpi, 1, 1, 1);
MODULE_VERSION(gpio_intel, 1);
