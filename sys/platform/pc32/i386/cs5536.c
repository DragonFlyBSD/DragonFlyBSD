/*
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2007 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
 *
 */

#include "use_gpio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/systimer.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"
#include <bus/pci/pcib_private.h>
#include <dev/misc/gpio/gpio.h>
#include <machine/pc/bios.h>
#include <sys/wdog.h>

#define AMD5536_TIMER_FREQ	3579545

#define	AMD5536_REV		0x51400017
#define	AMD5536_REV_MASK	0xff
#define	AMD5536_TMC		0x51400050

/* Multi-Functional General Purpose Timer */
#define	MSR_LBAR_MFGPT		0x5140000d
#define	AMD5536_MFGPT0_CMP1	0x00000000
#define	AMD5536_MFGPT0_CMP2	0x00000002
#define	AMD5536_MFGPT0_CNT	0x00000004
#define	AMD5536_MFGPT0_SETUP	0x00000006
#define	AMD5536_MFGPT_DIV_MASK	0x000f	/* div = 1 << mask */
#define	AMD5536_MFGPT_CLKSEL	0x0010
#define	AMD5536_MFGPT_REV_EN	0x0020
#define	AMD5536_MFGPT_CMP1DIS	0x0000
#define	AMD5536_MFGPT_CMP1EQ	0x0040
#define	AMD5536_MFGPT_CMP1GE	0x0080
#define	AMD5536_MFGPT_CMP1EV	0x00c0
#define	AMD5536_MFGPT_CMP2DIS	0x0000
#define	AMD5536_MFGPT_CMP2EQ	0x0100
#define	AMD5536_MFGPT_CMP2GE	0x0200
#define	AMD5536_MFGPT_CMP2EV	0x0300
#define	AMD5536_MFGPT_STOP_EN	0x0800
#define	AMD5536_MFGPT_SET	0x1000
#define	AMD5536_MFGPT_CMP1	0x2000
#define	AMD5536_MFGPT_CMP2	0x4000
#define	AMD5536_MFGPT_CNT_EN	0x8000
#define	AMD5536_MFGPT_IRQ	0x51400028
#define	AMD5536_MFGPT0_C1_IRQM	0x00000001
#define	AMD5536_MFGPT1_C1_IRQM	0x00000002
#define	AMD5536_MFGPT2_C1_IRQM	0x00000004
#define	AMD5536_MFGPT3_C1_IRQM	0x00000008
#define	AMD5536_MFGPT4_C1_IRQM	0x00000010
#define	AMD5536_MFGPT5_C1_IRQM	0x00000020
#define	AMD5536_MFGPT6_C1_IRQM	0x00000040
#define	AMD5536_MFGPT7_C1_IRQM	0x00000080
#define	AMD5536_MFGPT0_C2_IRQM	0x00000100
#define	AMD5536_MFGPT1_C2_IRQM	0x00000200
#define	AMD5536_MFGPT2_C2_IRQM	0x00000400
#define	AMD5536_MFGPT3_C2_IRQM	0x00000800
#define	AMD5536_MFGPT4_C2_IRQM	0x00001000
#define	AMD5536_MFGPT5_C2_IRQM	0x00002000
#define	AMD5536_MFGPT6_C2_IRQM	0x00004000
#define	AMD5536_MFGPT7_C2_IRQM	0x00008000
#define	AMD5536_MFGPT_NR	0x51400029
#define	AMD5536_MFGPT0_C1_NMIM	0x00000001
#define	AMD5536_MFGPT1_C1_NMIM	0x00000002
#define	AMD5536_MFGPT2_C1_NMIM	0x00000004
#define	AMD5536_MFGPT3_C1_NMIM	0x00000008
#define	AMD5536_MFGPT4_C1_NMIM	0x00000010
#define	AMD5536_MFGPT5_C1_NMIM	0x00000020
#define	AMD5536_MFGPT6_C1_NMIM	0x00000040
#define	AMD5536_MFGPT7_C1_NMIM	0x00000080
#define	AMD5536_MFGPT0_C2_NMIM	0x00000100
#define	AMD5536_MFGPT1_C2_NMIM	0x00000200
#define	AMD5536_MFGPT2_C2_NMIM	0x00000400
#define	AMD5536_MFGPT3_C2_NMIM	0x00000800
#define	AMD5536_MFGPT4_C2_NMIM	0x00001000
#define	AMD5536_MFGPT5_C2_NMIM	0x00002000
#define	AMD5536_MFGPT6_C2_NMIM	0x00004000
#define	AMD5536_MFGPT7_C2_NMIM	0x00008000
#define	AMD5536_NMI_LEG		0x00010000
#define	AMD5536_MFGPT0_C2_RSTEN	0x01000000
#define	AMD5536_MFGPT1_C2_RSTEN	0x02000000
#define	AMD5536_MFGPT2_C2_RSTEN	0x04000000
#define	AMD5536_MFGPT3_C2_RSTEN	0x08000000
#define	AMD5536_MFGPT4_C2_RSTEN	0x10000000
#define	AMD5536_MFGPT5_C2_RSTEN	0x20000000
#define	AMD5536_MFGPT_SETUP	0x5140002b

/* GPIO */
#define	MSR_LBAR_GPIO		0x5140000c
#define	AMD5536_GPIO_NPINS	32
#define	AMD5536_GPIOH_OFFSET	0x80	/* high bank register offset */
#define	AMD5536_GPIO_OUT_VAL	0x00	/* output value */
#define	AMD5536_GPIO_OUT_EN	0x04	/* output enable */
#define	AMD5536_GPIO_OD_EN	0x08	/* open-drain enable */
#define AMD5536_GPIO_OUT_INVRT_EN 0x0c	/* invert output */
#define	AMD5536_GPIO_PU_EN	0x18	/* pull-up enable */
#define	AMD5536_GPIO_PD_EN	0x1c	/* pull-down enable */
#define	AMD5536_GPIO_IN_EN	0x20	/* input enable */
#define AMD5536_GPIO_IN_INVRT_EN 0x24	/* invert input */
#define	AMD5536_GPIO_READ_BACK	0x30	/* read back value */


struct cs5536_softc {
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

#if NGPIO > 0
	/* GPIO interface */
	bus_space_tag_t		sc_gpio_iot;
	bus_space_handle_t	sc_gpio_ioh;
	struct gpio		sc_gpio_gc;
	gpio_pin_t		sc_gpio_pins[AMD5536_GPIO_NPINS];
#endif
};

static struct cs5536_softc cs5536_sc;

#if NGPIO > 0
int cs5536_gpio_pin_read(void *arg, int pin);
void cs5536_gpio_pin_write(void *arg, int pin, int value);
void cs5536_gpio_pin_ctl(void *arg, int pin, int flags);
#endif
static sysclock_t cs5536_get_timecount(void);

static struct bios_oem bios_soekris_55 = {
    { 0xf0000, 0xf1000 },
    {
	{ "Soekris", 0, 8 },		/* Soekris Engineering. */
	{ "net5", 0, 8 },		/* net5xxx */
	{ "comBIOS", 0, 54 },		/* comBIOS ver. 1.26a  20040819 ... */
	{ NULL, 0, 0 },
    }
};

static struct bios_oem bios_pcengines_55 = {
    { 0xf9000, 0xfa000 },
    {
	{ "PC Engines ALIX", 0, 28 },	/* PC Engines ALIX */
	{ "tinyBIOS", 0, 28 },		/* tinyBIOS V1.4a (C)1997-2005 */
	{ NULL, 0, 0 },
    }
};

#if NGPIO > 0
int
cs5536_gpio_pin_read(void *arg, int pin)
{
	uint32_t data;
	uint16_t port;
	int	reg, off = 0;

	port = rdmsr(MSR_LBAR_GPIO);

	reg = AMD5536_GPIO_IN_EN;
	if (pin > 15) {
		pin &= 0x0f;
		off = AMD5536_GPIOH_OFFSET;
	}
	reg += off;
	data = bus_space_read_4(cs5536_sc.sc_gpio_iot, cs5536_sc.sc_gpio_ioh, reg);

	if (data & (1 << pin))
		reg = AMD5536_GPIO_READ_BACK + off;
	else
		reg = AMD5536_GPIO_OUT_VAL + off;

	data = bus_space_read_4(cs5536_sc.sc_gpio_iot, cs5536_sc.sc_gpio_ioh, reg);

	return data & 1 << pin ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
cs5536_gpio_pin_write(void *arg, int pin, int value)
{
	uint32_t data;
	uint16_t port;
	int	reg;

	port = rdmsr(MSR_LBAR_GPIO);

	reg = AMD5536_GPIO_OUT_VAL;
	if (pin > 15) {
		pin &= 0x0f;
		reg += AMD5536_GPIOH_OFFSET;
	}

	if (value == 1)
		data = 1 << pin;
	else
		data = 1 << (pin + 16);

	bus_space_write_4(cs5536_sc.sc_gpio_iot, cs5536_sc.sc_gpio_ioh, reg, data);
	//write_data_4(port, reg, data);
	//outl(port + reg, data);
}

void
cs5536_gpio_pin_ctl(void *arg, int pin, int flags)
{
	int n, reg[7], val[7], nreg = 0, off = 0;

	if (pin > 15) {
		pin &= 0x0f;
		off = AMD5536_GPIOH_OFFSET;
	}

	reg[nreg] = AMD5536_GPIO_IN_EN + off;
	if (flags & GPIO_PIN_INPUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OUT_EN + off;
	if (flags & GPIO_PIN_OUTPUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OD_EN + off;
	if (flags & GPIO_PIN_OPENDRAIN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_PU_EN + off;
	if (flags & GPIO_PIN_PULLUP)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_PD_EN + off;
	if (flags & GPIO_PIN_PULLDOWN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_IN_INVRT_EN + off;
	if (flags & GPIO_PIN_INVIN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OUT_INVRT_EN + off;
	if (flags & GPIO_PIN_INVOUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	/* set flags */
	for (n = 0; n < nreg; n++)
		bus_space_write_4(cs5536_sc.sc_gpio_iot, cs5536_sc.sc_gpio_ioh, reg[n],
		    val[n]);
}
#endif /* NGPIO */

static void
cs5536_cputimer_construct(struct cputimer *timer, sysclock_t oldclock)
{
	timer->base = 0;
	timer->base = oldclock - cs5536_get_timecount();
}

static struct cputimer cs5536_timer = {
	SLIST_ENTRY_INITIALIZER,
	"CS5536",
	CPUTIMER_PRI_CS5536,
	CPUTIMER_GEODE,
	cs5536_get_timecount,
	cputimer_default_fromhz,
	cputimer_default_fromus,
	cs5536_cputimer_construct,
	cputimer_default_destruct,
	AMD5536_TIMER_FREQ,
	0, 0, 0
};

static sysclock_t
cs5536_get_timecount(void)
{
	return (cs5536_timer.base + rdmsr(AMD5536_TMC));
}

static int
cs5536_watchdog(void *unused, int period)
{
	if (period > 0xffff)
		period = 0xffff;

	bus_space_write_2(cs5536_sc.sc_iot, cs5536_sc.sc_ioh, AMD5536_MFGPT0_SETUP,
		    AMD5536_MFGPT_CNT_EN | AMD5536_MFGPT_CMP2);
	/* reset counter */
	bus_space_write_2(cs5536_sc.sc_iot, cs5536_sc.sc_ioh, AMD5536_MFGPT0_CNT, 0);
	/* set comparator 2 */
	bus_space_write_2(cs5536_sc.sc_iot, cs5536_sc.sc_ioh, AMD5536_MFGPT0_CMP2, period);

	if (period) {
		wrmsr(AMD5536_MFGPT_NR,
		    rdmsr(AMD5536_MFGPT_NR) | AMD5536_MFGPT0_C2_RSTEN);
	} else {
		wrmsr(AMD5536_MFGPT_NR,
		    rdmsr(AMD5536_MFGPT_NR) & ~AMD5536_MFGPT0_C2_RSTEN);
	}

	return period;
}

static struct watchdog	cs5536_wdog = {
	.name		=	"AMD CS5536",
	.wdog_fn	=	cs5536_watchdog,
	.arg		=	NULL,
	.period_max	=	0xffff,
};

static int
cs5536_probe(device_t self)
{
	static int probed = 0;

	if (probed)
		return ENXIO;

	if (pci_get_vendor(self) == PCI_VENDOR_AMD &&
		(pci_get_device(self) == PCI_PRODUCT_AMD_CS5536_PCIB ||
		/* XXX: OpenBSD doesn't attach to this one, but free does, though no counter */
		pci_get_device(self) == PCI_PRODUCT_AMD_GEODE_LX_PCHB)) {
		/* device_set_desc(self, ...) */
		probed = 1;
		return 0;
	}

	return ENXIO;
}

static int
cs5536_attach(device_t self)
{
#define BIOS_OEM_MAXLEN 80
	static u_char bios_oem[BIOS_OEM_MAXLEN] = "\0";
	static int attached = 0;
#if NGPIO > 0
	int i;
#endif
#if 0 /* Watchdog stuff */
	EVENTHANDLER_REGISTER(watchdog_list, cs5536_watchdog, NULL, 0);
#endif
	if (attached)
		return ENODEV;

	attached = 1;
	kprintf("AMD CS5536: rev %d, 32-bit %uHz timer\n",
		(int)rdmsr(AMD5536_REV) & AMD5536_REV_MASK,
		cs5536_timer.freq);

	/* enable timer */
	cputimer_register(&cs5536_timer);
	cputimer_select(&cs5536_timer, 0);

	/* bus_space_map(sc->sc_iot, wa & 0xffff, 64, 0, &sc->sc_ioh)) */
	cs5536_sc.sc_iot = I386_BUS_SPACE_IO;
	cs5536_sc.sc_ioh = rdmsr(MSR_LBAR_MFGPT);

	/* enable watchdog and configure */
	bus_space_write_2(cs5536_sc.sc_iot, cs5536_sc.sc_ioh, AMD5536_MFGPT0_SETUP,
		AMD5536_MFGPT_CNT_EN | AMD5536_MFGPT_CMP2EV |
		AMD5536_MFGPT_CMP2 | AMD5536_MFGPT_DIV_MASK);
	wdog_register(&cs5536_wdog);

#if NGPIO > 0
	/* bus_space_map(sc->sc_gpio_iot, ga & 0xffff, 0xff, 0,... */
	cs5536_sc.sc_gpio_iot = I386_BUS_SPACE_IO;
	cs5536_sc.sc_gpio_ioh = rdmsr(MSR_LBAR_GPIO);
	for (i = 0; i < AMD5536_GPIO_NPINS; i++) {
		cs5536_sc.sc_gpio_pins[i].pin_num = i;
		cs5536_sc.sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN |
			GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN |
			GPIO_PIN_INVIN | GPIO_PIN_INVOUT;

		/* read initial state */
		cs5536_sc.sc_gpio_pins[i].pin_state =
		    cs5536_gpio_pin_read(&cs5536_sc, i);
	}
	cs5536_sc.sc_gpio_gc.driver_name = "cs5536";
	cs5536_sc.sc_gpio_gc.arg = &cs5536_sc;
	cs5536_sc.sc_gpio_gc.pin_read = cs5536_gpio_pin_read;
	cs5536_sc.sc_gpio_gc.pin_write = cs5536_gpio_pin_write;
	cs5536_sc.sc_gpio_gc.pin_ctl = cs5536_gpio_pin_ctl;
	cs5536_sc.sc_gpio_gc.pins = cs5536_sc.sc_gpio_pins;
	cs5536_sc.sc_gpio_gc.npins = AMD5536_GPIO_NPINS;
	gpio_register(&cs5536_sc.sc_gpio_gc);
#endif
	if (bios_oem_strings(&bios_soekris_55,
	    bios_oem, sizeof bios_oem) > 0 ) {
#if NGPIO > 0
		/* Attach led to pin 6 */
		gpio_consumer_attach("led", "error", &cs5536_sc.sc_gpio_gc,
	        6, 1);
#endif
	} else if (bios_oem_strings(&bios_pcengines_55,
	    bios_oem, sizeof bios_oem) > 0 ) {
#if NGPIO > 0
		/* Attach leds */
		gpio_consumer_attach("led", "led1", &cs5536_sc.sc_gpio_gc,
	        6, 1);
		gpio_consumer_attach("led", "led2", &cs5536_sc.sc_gpio_gc,
	        25, 1);
		gpio_consumer_attach("led", "led3", &cs5536_sc.sc_gpio_gc,
	        27, 1);
#endif
#if 0
		/*
		* Turn on first LED so we don't make
		* people think their box just died.
		*/
		cs5536_led_func(&led1b, 1);
#endif
	}
	if (*bios_oem)
		kprintf("Geode LX: %s\n", bios_oem);
	else
		kprintf("Geode LX: Unknown OEM bios\n");

	if (bootverbose)
		kprintf("MFGPT bar: %jx\n", rdmsr(MSR_LBAR_MFGPT));

	return 0;
}

static device_method_t cs5536_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cs5536_probe),
	DEVMETHOD(device_attach,	cs5536_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static driver_t cs5536_driver = {
	"cs5536",
	cs5536_methods,
	0,
};

static devclass_t cs5536_devclass;

DRIVER_MODULE(cs5536, pci, cs5536_driver, cs5536_devclass, NULL, NULL);
