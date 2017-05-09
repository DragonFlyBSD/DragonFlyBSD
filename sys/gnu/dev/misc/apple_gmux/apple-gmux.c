/*
 * apple_gmux - Gmux driver for DragonFly
 *
 * Adapted from linux v4.8: drivers/platform/x86/apple-gmux.c
 *
 */

/*
 *  Gmux driver for Apple laptops
 *
 *  Copyright (C) Canonical Ltd. <seth.forshee@canonical.com>
 *  Copyright (C) 2010-2012 Andreas Heider <andreas@meetr.de>
 *  Copyright (C) 2015 Lukas Wunner <lukas@wunner.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/rman.h>

#define pr_fmt(fmt) "apple_gmux: " fmt

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/backlight.h>
#include <linux/apple-gmux.h>
#include <linux/delay.h>
#include <linux/completion.h>

#include <drm/drmP.h>
#ifndef VGA_SWITCHEROO
#define	VGA_SWITCHEROO	1	/* Must be always defined */
#endif
#include <linux/vga_switcheroo.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "opt_acpi.h"
#include <contrib/dev/acpica/source/include/acpi.h>
#include <contrib/dev/acpica/source/include/accommon.h>
#include <dev/acpica/acpivar.h>

/*
 * gmux port offsets. Many of these are not yet used, but may be in the
 * future, and it's useful to have them documented here anyhow.
 */
#define GMUX_PORT_VERSION_MAJOR		0x04
#define GMUX_PORT_VERSION_MINOR		0x05
#define GMUX_PORT_VERSION_RELEASE	0x06
#define GMUX_PORT_SWITCH_DISPLAY	0x10
#define GMUX_PORT_SWITCH_GET_DISPLAY	0x11
#define GMUX_PORT_INTERRUPT_ENABLE	0x14
#define GMUX_PORT_INTERRUPT_STATUS	0x16
#define GMUX_PORT_SWITCH_DDC		0x28
#define GMUX_PORT_SWITCH_EXTERNAL	0x40
#define GMUX_PORT_SWITCH_GET_EXTERNAL	0x41
#define GMUX_PORT_DISCRETE_POWER	0x50
#define GMUX_PORT_MAX_BRIGHTNESS	0x70
#define GMUX_PORT_BRIGHTNESS		0x74
#define GMUX_PORT_VALUE			0xc2
#define GMUX_PORT_READ			0xd0
#define GMUX_PORT_WRITE			0xd4

#define GMUX_MIN_IO_LEN			(GMUX_PORT_BRIGHTNESS + 4)

#define GMUX_INTERRUPT_ENABLE		0xff
#define GMUX_INTERRUPT_DISABLE		0x00

#define GMUX_INTERRUPT_STATUS_ACTIVE	0
#define GMUX_INTERRUPT_STATUS_DISPLAY	(1 << 0)
#define GMUX_INTERRUPT_STATUS_POWER	(1 << 2)
#define GMUX_INTERRUPT_STATUS_HOTPLUG	(1 << 3)

#define GMUX_BRIGHTNESS_MASK		0x00ffffff
#define GMUX_MAX_BRIGHTNESS		GMUX_BRIGHTNESS_MASK

/**
 * DOC: Overview
 *
 * :1:  http://www.latticesemi.com/en/Products/FPGAandCPLD/LatticeXP2.aspx
 * :2:  http://www.renesas.com/products/mpumcu/h8s/h8s2100/h8s2113/index.jsp
 *
 * gmux is a microcontroller built into the MacBook Pro to support dual GPUs:
 * A {1}[Lattice XP2] on pre-retinas, a {2}[Renesas R4F2113] on retinas.
 *
 * (The MacPro6,1 2013 also has a gmux, however it is unclear why since it has
 * dual GPUs but no built-in display.)
 *
 * gmux is connected to the LPC bus of the southbridge. Its I/O ports are
 * accessed differently depending on the microcontroller: Driver functions
 * to access a pre-retina gmux are infixed `_pio_`, those for a retina gmux
 * are infixed `_index_`.
 */

struct apple_gmux_softc {
	device_t gmux_dev;
	int io_rid;
	struct resource *io_res;
	bool indexed;
	struct lock lk;

	int unit;

	struct pci_dev *pdev;
	/* struct backlight_device *bdev; */

	/* switcheroo data */
	ACPI_HANDLE dhandle;
	int gpe;
	enum vga_switcheroo_client_id switch_state_display;
	enum vga_switcheroo_client_id switch_state_ddc;
	enum vga_switcheroo_client_id switch_state_external;
	enum vga_switcheroo_state power_state;
	struct completion powerchange_done;
};

static struct apple_gmux_softc *apple_gmux_softc_data;

static void		apple_gmux_lock(struct apple_gmux_softc *sc);
static void		apple_gmux_unlock(struct apple_gmux_softc *sc);
static u8		gmux_pio_read8(struct apple_gmux_softc *sc, uint8_t offset);
static void		gmux_pio_write8(struct apple_gmux_softc *sc, int offset, uint8_t value);
static u32		gmux_pio_read32(struct apple_gmux_softc *sc, uint8_t offset);
//static void		gmux_pio_write32(struct apple_gmux_softc *sc, uint8_t offset, uint32_t value);
static int		gmux_index_wait_ready(struct apple_gmux_softc *sc);
static int		gmux_index_wait_complete(struct apple_gmux_softc *sc);
static u8		gmux_index_read8(struct apple_gmux_softc *sc, uint8_t offset);
static void		gmux_index_write8(struct apple_gmux_softc *sc, int offset, uint8_t value);
static u32		gmux_index_read32(struct apple_gmux_softc *sc, uint8_t offset);
//static void		gmux_index_write32(struct apple_gmux_softc *sc, int offset, uint32_t value);
static u8		gmux_read8(struct apple_gmux_softc *sc, int offset);
static void		gmux_write8(struct apple_gmux_softc *sc, int offset, uint8_t value);
static u32		gmux_read32(struct apple_gmux_softc *sc, int offset);
//static void		gmux_write32(struct apple_gmux_softc *sc, int offset, uint32_t value);
static bool		gmux_is_indexed(struct apple_gmux_softc *sc);
static void		gmux_read_switch_state(struct apple_gmux_softc *sc);
static void		gmux_write_switch_state(struct apple_gmux_softc *sc);
static int		gmux_switchto(enum vga_switcheroo_client_id id);
static int		gmux_switch_ddc(enum vga_switcheroo_client_id id);
static int		gmux_set_discrete_state(struct apple_gmux_softc *sc, enum vga_switcheroo_state state);
static int		gmux_set_power_state(enum vga_switcheroo_client_id id, enum vga_switcheroo_state state);
static inline void	gmux_disable_interrupts(struct apple_gmux_softc *sc);
static inline void	gmux_enable_interrupts(struct apple_gmux_softc *sc);
static inline u8	gmux_interrupt_get_status(struct apple_gmux_softc *sc);
static void		gmux_clear_interrupts(struct apple_gmux_softc *sc);
static void		gmux_notify_handler(ACPI_HANDLE device, UINT32 value, void *context);
static int		apple_gmux_suspend(device_t dev);
static int		apple_gmux_resume(device_t dev);
device_t		gmux_get_io_pdev(void);
static int		apple_gmux_probe(device_t dev);
static int		apple_gmux_attach(device_t dev);
static int		apple_gmux_detach(device_t dev);

static void
apple_gmux_lock(struct apple_gmux_softc *sc)
{
	lockmgr(&sc->lk, LK_EXCLUSIVE);
}

static void
apple_gmux_unlock(struct apple_gmux_softc *sc)
{
	lockmgr(&sc->lk, LK_RELEASE);
}

static u8
gmux_pio_read8(struct apple_gmux_softc *sc, uint8_t offset)
{
	return(bus_read_1(sc->io_res, offset));
}

static void
gmux_pio_write8(struct apple_gmux_softc *sc, int offset, uint8_t value)
{
	bus_write_1(sc->io_res, offset, value);
}

static u32
gmux_pio_read32(struct apple_gmux_softc *sc, uint8_t offset)
{
	return(bus_read_4(sc->io_res, offset));
}

#if 0  /* Defined but not used */
static void
gmux_pio_write32(struct apple_gmux_softc *sc, uint8_t offset,
			     uint32_t value)
{
	int i;
	u8 tmpval;

	for (i = 0; i < 4; i++) {
		tmpval = (value >> (i * 8)) & 0xff;
		bus_write_1(sc->io_res, offset + i, tmpval);
	}
}
#endif  /* Defined but not used */

static int
gmux_index_wait_ready(struct apple_gmux_softc *sc)
{
	int i = 200;
	u8 gwr = bus_read_1(sc->io_res, GMUX_PORT_WRITE);

	while (i && (gwr & 0x01)) {
		bus_read_1(sc->io_res, GMUX_PORT_READ);
		gwr = bus_read_1(sc->io_res, GMUX_PORT_WRITE);
		udelay(100);
		i--;
	}

	return !!i;
}

static int
gmux_index_wait_complete(struct apple_gmux_softc *sc)
{
	int i = 200;
	u8 gwr = bus_read_1(sc->io_res, GMUX_PORT_WRITE);

	while (i && !(gwr & 0x01)) {
		gwr = bus_read_1(sc->io_res, GMUX_PORT_WRITE);
		udelay(100);
		i--;
	}

	if (gwr & 0x01)
		bus_read_1(sc->io_res, GMUX_PORT_READ);

	return !!i;
}

static u8
gmux_index_read8(struct apple_gmux_softc *sc, uint8_t offset)
{
	u8 val;

	apple_gmux_lock(sc);
	gmux_index_wait_ready(sc);
	bus_write_1(sc->io_res, GMUX_PORT_READ, (offset & 0xff));
	gmux_index_wait_complete(sc);
	val = bus_read_1(sc->io_res, GMUX_PORT_VALUE);
	apple_gmux_unlock(sc);

	return val;
}

static void
gmux_index_write8(struct apple_gmux_softc *sc, int offset, uint8_t value)
{
	apple_gmux_lock(sc);
	bus_write_1(sc->io_res, GMUX_PORT_VALUE, value);
	gmux_index_wait_ready(sc);
	bus_write_1(sc->io_res, GMUX_PORT_WRITE, (offset & 0xff));
	gmux_index_wait_complete(sc);
	apple_gmux_unlock(sc);
}

static u32
gmux_index_read32(struct apple_gmux_softc *sc, uint8_t offset)
{
	u32 val;

	apple_gmux_lock(sc);
	gmux_index_wait_ready(sc);
	bus_write_1(sc->io_res, GMUX_PORT_READ, (offset & 0xff));
	gmux_index_wait_complete(sc);
	val = bus_read_4(sc->io_res, GMUX_PORT_VALUE);
	apple_gmux_unlock(sc);

	return val;
}

#if 0  /* Defined but not used */
static void
gmux_index_write32(struct apple_gmux_softc *sc, int offset, uint32_t value)
{
	int i;
	u8 tmpval;

	apple_gmux_lock(sc);

	for (i = 0; i < 4; i++) {
		tmpval = (value >> (i * 8)) & 0xff;
		bus_write_1(sc->io_res, GMUX_PORT_VALUE + i, tmpval);
	}

	gmux_index_wait_ready(sc);
	bus_write_1(sc->io_res, GMUX_PORT_WRITE, (offset & 0xff));
	gmux_index_wait_complete(sc);
	apple_gmux_unlock(sc);
}
#endif  /* Defined but not used */

static u8
gmux_read8(struct apple_gmux_softc *sc, int offset)
{
	if (sc->indexed)
		return gmux_index_read8(sc, offset);
	else
		return gmux_pio_read8(sc, offset);
}

static void
gmux_write8(struct apple_gmux_softc *sc, int offset, uint8_t value)
{
	if (sc->indexed)
		gmux_index_write8(sc, offset, value);
	else
		gmux_pio_write8(sc, offset, value);
}

static u32
gmux_read32(struct apple_gmux_softc *sc, int offset)
{
	if (sc->indexed)
		return gmux_index_read32(sc, offset);
	else
		return gmux_pio_read32(sc, offset);
}

#if 0  /* Defined but not used */
static void
gmux_write32(struct apple_gmux_softc *sc, int offset, uint32_t value)
{
	if (sc->indexed)
		gmux_index_write32(sc, offset, value);
	else
		gmux_pio_write32(sc, offset, value);
}
#endif  /* Defined but not used */

static bool
gmux_is_indexed(struct apple_gmux_softc *sc)
{
	u16 val;

	bus_write_1(sc->io_res, 0xcc, 0xaa);
	bus_write_1(sc->io_res, 0xcd, 0x55);
	bus_write_1(sc->io_res, 0xce, 0x00);

	val = bus_read_1(sc->io_res, 0xcc) |
		(bus_read_1(sc->io_res, 0xcd) << 8);

	if (val == 0x55aa)
		return true;

	return(false);
}

/**
 * DOC: Backlight control
 *
 * :3:  http://www.ti.com/lit/ds/symlink/lp8543.pdf
 * :4:  http://www.ti.com/lit/ds/symlink/lp8545.pdf
 *
 * On single GPU MacBooks, the PWM signal for the backlight is generated by
 * the GPU. On dual GPU MacBook Pros by contrast, either GPU may be suspended
 * to conserve energy. Hence the PWM signal needs to be generated by a separate
 * backlight driver which is controlled by gmux. The earliest generation
 * MBP5 2008/09 uses a {3}[TI LP8543] backlight driver. All newer models
 * use a {4}[TI LP8545].
 */

#if 0  /* no backlight */
static int
gmux_get_brightness(struct backlight_device *bd)
{
	struct apple_gmux_softc *sc = bl_get_data(bd);
	return gmux_read32(sc, GMUX_PORT_BRIGHTNESS) &
	       GMUX_BRIGHTNESS_MASK;
}

static int
gmux_update_status(struct backlight_device *bd)
{
	struct apple_gmux_softc *sc = bl_get_data(bd);
	u32 brightness = bd->props.brightness;

	if (bd->props.state & BL_CORE_SUSPENDED)
		return 0;

	gmux_write32(sc, GMUX_PORT_BRIGHTNESS, brightness);

	return 0;
}

static const struct backlight_ops gmux_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = gmux_get_brightness,
	.update_status = gmux_update_status,
};
#endif  /* no backlight */

/**
 * DOC: Graphics mux
 *
 * :5:  http://pimg-fpiw.uspto.gov/fdd/07/870/086/0.pdf
 * :6:  http://www.nxp.com/documents/data_sheet/CBTL06141.pdf
 * :7:  http://www.ti.com/lit/ds/symlink/hd3ss212.pdf
 * :8:  https://www.pericom.com/assets/Datasheets/PI3VDP12412.pdf
 * :9:  http://www.ti.com/lit/ds/symlink/sn74lv4066a.pdf
 * :10: http://pdf.datasheetarchive.com/indexerfiles/Datasheets-SW16/DSASW00308511.pdf
 * :11: http://www.ti.com/lit/ds/symlink/ts3ds10224.pdf
 *
 * On pre-retinas, the LVDS outputs of both GPUs feed into gmux which muxes
 * either of them to the panel. One of the tricks gmux has up its sleeve is
 * to lengthen the blanking interval of its output during a switch to
 * synchronize it with the GPU switched to. This allows for a flicker-free
 * switch that is imperceptible by the user ({5}[US 8,687,007 B2]).
 *
 * On retinas, muxing is no longer done by gmux itself, but by a separate
 * chip which is controlled by gmux. The chip is triple sourced, it is
 * either an {6}[NXP CBTL06142], {7}[TI HD3SS212] or {8}[Pericom PI3VDP12412].
 * The panel is driven with eDP instead of LVDS since the pixel clock
 * required for retina resolution exceeds LVDS' limits.
 *
 * Pre-retinas are able to switch the panel's DDC pins separately.
 * This is handled by a {9}[TI SN74LV4066A] which is controlled by gmux.
 * The inactive GPU can thus probe the panel's EDID without switching over
 * the entire panel. Retinas lack this functionality as the chips used for
 * eDP muxing are incapable of switching the AUX channel separately (see
 * the linked data sheets, Pericom would be capable but this is unused).
 * However the retina panel has the NO_AUX_HANDSHAKE_LINK_TRAINING bit set
 * in its DPCD, allowing the inactive GPU to skip the AUX handshake and
 * set up the output with link parameters pre-calibrated by the active GPU.
 *
 * The external DP port is only fully switchable on the first two unibody
 * MacBook Pro generations, MBP5 2008/09 and MBP6 2010. This is done by an
 * {6}[NXP CBTL06141] which is controlled by gmux. It's the predecessor of the
 * eDP mux on retinas, the difference being support for 2.7 versus 5.4 Gbit/s.
 *
 * The following MacBook Pro generations replaced the external DP port with a
 * combined DP/Thunderbolt port and lost the ability to switch it between GPUs,
 * connecting it either to the discrete GPU or the Thunderbolt controller.
 * Oddly enough, while the full port is no longer switchable, AUX and HPD
 * are still switchable by way of an {10}[NXP CBTL03062] (on pre-retinas
 * MBP8 2011 and MBP9 2012) or two {11}[TI TS3DS10224] (on retinas) under the
 * control of gmux. Since the integrated GPU is missing the main link,
 * external displays appear to it as phantoms which fail to link-train.
 *
 * gmux receives the HPD signal of all display connectors and sends an
 * interrupt on hotplug. On generations which cannot switch external ports,
 * the discrete GPU can then be woken to drive the newly connected display.
 * The ability to switch AUX on these generations could be used to improve
 * reliability of hotplug detection by having the integrated GPU poll the
 * ports while the discrete GPU is asleep, but currently we do not make use
 * of this feature.
 *
 * gmux' initial switch state on bootup is user configurable via the EFI
 * variable `gpu-power-prefs-fa4ce28d-b62f-4c99-9cc3-6815686e30f9` (5th byte,
 * 1 = IGD, 0 = DIS). Based on this setting, the EFI firmware tells gmux to
 * switch the panel and the external DP connector and allocates a framebuffer
 * for the selected GPU.
 */

static void
gmux_read_switch_state(struct apple_gmux_softc *sc)
{
	if (gmux_read8(sc, GMUX_PORT_SWITCH_DDC) == 1)
		sc->switch_state_ddc = VGA_SWITCHEROO_IGD;
	else
		sc->switch_state_ddc = VGA_SWITCHEROO_DIS;

	if (gmux_read8(sc, GMUX_PORT_SWITCH_DISPLAY) == 2)
		sc->switch_state_display = VGA_SWITCHEROO_IGD;
	else
		sc->switch_state_display = VGA_SWITCHEROO_DIS;

	if (gmux_read8(sc, GMUX_PORT_SWITCH_EXTERNAL) == 2)
		sc->switch_state_external = VGA_SWITCHEROO_IGD;
	else
		sc->switch_state_external = VGA_SWITCHEROO_DIS;
}

static void
gmux_write_switch_state(struct apple_gmux_softc *sc)
{
	if (sc->switch_state_ddc == VGA_SWITCHEROO_IGD)
		gmux_write8(sc, GMUX_PORT_SWITCH_DDC, 1);
	else
		gmux_write8(sc, GMUX_PORT_SWITCH_DDC, 2);

	if (sc->switch_state_display == VGA_SWITCHEROO_IGD)
		gmux_write8(sc, GMUX_PORT_SWITCH_DISPLAY, 2);
	else
		gmux_write8(sc, GMUX_PORT_SWITCH_DISPLAY, 3);

	if (sc->switch_state_external == VGA_SWITCHEROO_IGD)
		gmux_write8(sc, GMUX_PORT_SWITCH_EXTERNAL, 2);
	else
		gmux_write8(sc, GMUX_PORT_SWITCH_EXTERNAL, 3);
}

static int
gmux_switchto(enum vga_switcheroo_client_id id)
{
	apple_gmux_softc_data->switch_state_ddc = id;
	apple_gmux_softc_data->switch_state_display = id;
	apple_gmux_softc_data->switch_state_external = id;

	gmux_write_switch_state(apple_gmux_softc_data);

	return 0;
}

static int
gmux_switch_ddc(enum vga_switcheroo_client_id id)
{
	enum vga_switcheroo_client_id old_ddc_owner =
		apple_gmux_softc_data->switch_state_ddc;

	if (id == old_ddc_owner)
		return id;

	pr_debug("Switching DDC from %d to %d\n", old_ddc_owner, id);
	apple_gmux_softc_data->switch_state_ddc = id;

	if (id == VGA_SWITCHEROO_IGD)
		gmux_write8(apple_gmux_softc_data, GMUX_PORT_SWITCH_DDC, 1);
	else
		gmux_write8(apple_gmux_softc_data, GMUX_PORT_SWITCH_DDC, 2);

	return old_ddc_owner;
}

/**
 * DOC: Power control
 *
 * gmux is able to cut power to the discrete GPU. It automatically takes care
 * of the correct sequence to tear down and bring up the power rails for
 * core voltage, VRAM and PCIe.
 */

static int
gmux_set_discrete_state(struct apple_gmux_softc *sc,
		enum vga_switcheroo_state state)
{
	reinit_completion(&sc->powerchange_done);

	if (state == VGA_SWITCHEROO_ON) {
		gmux_write8(sc, GMUX_PORT_DISCRETE_POWER, 1);
		gmux_write8(sc, GMUX_PORT_DISCRETE_POWER, 3);
		pr_debug("Discrete card powered up\n");
	} else {
		gmux_write8(sc, GMUX_PORT_DISCRETE_POWER, 1);
		gmux_write8(sc, GMUX_PORT_DISCRETE_POWER, 0);
		pr_debug("Discrete card powered down\n");
	}

	sc->power_state = state;

	if (sc->gpe >= 0 &&
	    !wait_for_completion_interruptible_timeout(&sc->powerchange_done,
						       msecs_to_jiffies(200)))
		pr_warn("Timeout waiting for gmux switch to complete\n");

	return 0;
}

static int
gmux_set_power_state(enum vga_switcheroo_client_id id,
		enum vga_switcheroo_state state)
{
	if (id == VGA_SWITCHEROO_IGD)
		return 0;

	return gmux_set_discrete_state(apple_gmux_softc_data, state);
}

static int
gmux_get_client_id(struct pci_dev *pdev)
{
	/*
	 * Early Macbook Pros with switchable graphics use nvidia
	 * integrated graphics. Hardcode that the 9400M is integrated.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		return VGA_SWITCHEROO_IGD;
	else if (pdev->vendor == PCI_VENDOR_ID_NVIDIA &&
		 pdev->device == 0x0863)
		return VGA_SWITCHEROO_IGD;
	else
		return VGA_SWITCHEROO_DIS;
}

static const struct vga_switcheroo_handler gmux_handler_indexed = {
	.switchto = gmux_switchto,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

static const struct vga_switcheroo_handler gmux_handler_classic = {
	.switchto = gmux_switchto,
	.switch_ddc = gmux_switch_ddc,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

/**
 * DOC: Interrupt
 *
 * gmux is also connected to a GPIO pin of the southbridge and thereby is able
 * to trigger an ACPI GPE. On the MBP5 2008/09 it's GPIO pin 22 of the Nvidia
 * MCP79, on all following generations it's GPIO pin 6 of the Intel PCH.
 * The GPE merely signals that an interrupt occurred, the actual type of event
 * is identified by reading a gmux register.
 */

static inline void
gmux_disable_interrupts(struct apple_gmux_softc *sc)
{
	gmux_write8(sc, GMUX_PORT_INTERRUPT_ENABLE, GMUX_INTERRUPT_DISABLE);
}

static inline void
gmux_enable_interrupts(struct apple_gmux_softc *sc)
{
	gmux_write8(sc, GMUX_PORT_INTERRUPT_ENABLE, GMUX_INTERRUPT_ENABLE);
}

static inline u8
gmux_interrupt_get_status(struct apple_gmux_softc *sc)
{
	return gmux_read8(sc, GMUX_PORT_INTERRUPT_STATUS);
}

static void
gmux_clear_interrupts(struct apple_gmux_softc *sc)
{
	u8 status;

	/* to clear interrupts write back current status */
	status = gmux_interrupt_get_status(sc);
	gmux_write8(sc, GMUX_PORT_INTERRUPT_STATUS, status);
}

static void
gmux_notify_handler(ACPI_HANDLE device, UINT32 value, void *context)
{
	u8 status;
	device_t dev = context;
	struct apple_gmux_softc *sc = device_get_softc(dev);

	status = gmux_interrupt_get_status(sc);
	gmux_disable_interrupts(sc);
	pr_debug("Notify handler called: status %d\n", status);

	gmux_clear_interrupts(sc);
	gmux_enable_interrupts(sc);

	if (status & GMUX_INTERRUPT_STATUS_POWER)
		complete(&sc->powerchange_done);
}

static int
apple_gmux_suspend(device_t dev)
{
	/* XXX: TODO */

	return 0;
}

static int
apple_gmux_resume(device_t dev)
{
	/* XXX: TODO */

	return 0;
}

device_t
gmux_get_io_pdev(void)
{
	device_t pch = NULL;
	struct pci_devinfo *di = NULL;

	while ((pch = pci_iterate_class(&di, PCIC_DISPLAY, PCIS_DISPLAY_VGA))) {
		u16 cmd;

		cmd = (u16)pci_read_config(pch, PCIR_COMMAND, 2);
		if (!(cmd & PCIM_CMD_PORTEN))
			continue;

		return(pch);
	}

	return(NULL);
}

static int
apple_gmux_probe(device_t dev)
{
	device_t bus;
	static char *gmux_ids[] = { GMUX_ACPI_HID, NULL };

	bus = device_get_parent(dev);

	if (ACPI_ID_PROBE(bus, dev, gmux_ids) == NULL)
		return(ENXIO);

	device_set_desc(dev, "apple gmux controller");

	return(BUS_PROBE_DEFAULT);
}

static int
apple_gmux_attach(device_t dev)
{
	struct apple_gmux_softc *sc;
	int unit;
	uint8_t ver_major, ver_minor, ver_release;
	int ret = -ENXIO;
	ACPI_STATUS status;
	UINT32 gpe;
	device_t pch_dev = NULL;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	sc->gmux_dev = dev;
	sc->unit = unit;

	sc->io_rid = 0;
	sc->io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->io_rid,
					    RF_ACTIVE);

	if (sc->io_res == NULL) {
		pr_err("Failed to find gmux I/O resource\n");
		return(ENXIO);
	}

	/*
	 * Explicitly initialize sc->indexed, to be clear we assume
	 * gmux is classic by default.
	 */
	sc->indexed = false;

	/*
	 * Invalid version information may indicate either that the gmux
	 * device isn't present or that it's a new one that uses indexed
	 * io
	 */

	ver_major = gmux_read8(sc, GMUX_PORT_VERSION_MAJOR);
	ver_minor = gmux_read8(sc, GMUX_PORT_VERSION_MINOR);
	ver_release = gmux_read8(sc, GMUX_PORT_VERSION_RELEASE);
	if (ver_major == 0xff && ver_minor == 0xff && ver_release == 0xff) {
		if (gmux_is_indexed(sc)) {
			u32 version;
			lockinit(&sc->lk, "apple_gmux", 0, 0);
			sc->indexed = true;
			version = gmux_read32(sc, GMUX_PORT_VERSION_MAJOR);
			ver_major = (version >> 24) & 0xff;
			ver_minor = (version >> 16) & 0xff;
			ver_release = (version >> 8) & 0xff;
		} else {
			pr_info("gmux device not present or IO disabled\n");
			ret = -ENODEV;
			goto err_release;
		}
	}
	pr_info("Found gmux version %d.%d.%d [%s]\n", ver_major, ver_minor,
		ver_release, (sc->indexed ? "indexed" : "classic"));

	/*
	 * Apple systems with gmux are EFI based and normally don't use
	 * VGA. In addition changing IO+MEM ownership between IGP and dGPU
	 * disables IO/MEM used for backlight control on some systems.
	 * Lock IO+MEM to GPU with active IO to prevent switch.
	 */
	pch_dev = gmux_get_io_pdev();
	if (pch_dev == NULL) {
		ret = -ENODEV;
		pr_err("Cannot find PCI device\n");
		goto err_release;
	}
	drm_init_pdev(pch_dev, &sc->pdev);

#if 0  /* no backlight infrastructure */
	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = gmux_read32(gmux_data, GMUX_PORT_MAX_BRIGHTNESS);

	/*
	 * Currently it's assumed that the maximum brightness is less than
	 * 2^24 for compatibility with old gmux versions. Cap the max
	 * brightness at this value, but print a warning if the hardware
	 * reports something higher so that it can be fixed.
	 */
	if (WARN_ON(props.max_brightness > GMUX_MAX_BRIGHTNESS))
		props.max_brightness = GMUX_MAX_BRIGHTNESS;

	bdev = backlight_device_register("gmux_backlight", &pnp->dev,
					 gmux_data, &gmux_bl_ops, &props);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto err_release;
	}

	gmux_data->bdev = bdev;
	bdev->props.brightness = gmux_get_brightness(bdev);
	backlight_update_status(bdev);

	/*
	 * The backlight situation on Macs is complicated. If the gmux is
	 * present it's the best choice, because it always works for
	 * backlight control and supports more levels than other options.
	 * Disable the other backlight choices.
	 */
	acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);
	apple_bl_unregister();
#endif  /* no backlight infrastructure */

	sc->power_state = VGA_SWITCHEROO_ON;

	sc->dhandle = acpi_get_handle(dev);
	if (!sc->dhandle) {
		pr_err("Cannot find acpi handle for pnp device %s\n",
		       device_get_name(dev));
		ret = -ENODEV;
		goto err_notify;
	}

	status = acpi_GetInteger(sc->dhandle, "GMGP", &gpe);
	if (ACPI_SUCCESS(status)) {
		sc->gpe = (int)gpe;

		status = AcpiInstallNotifyHandler(sc->dhandle,
						  ACPI_DEVICE_NOTIFY,
						  gmux_notify_handler, dev);

		if (ACPI_FAILURE(status)) {
			pr_err("AcpiInstallNotifyHandler failed: %s\n",
			       AcpiFormatException(status));
			ret = -ENODEV;
			goto err_notify;
		}

		status = AcpiEnableGpe(NULL, sc->gpe);
		if (ACPI_FAILURE(status)) {
			pr_err("AcpiEnableGpe failed: %s\n",
			       AcpiFormatException(status));
			goto err_enable_gpe;
		}
	} else {
		pr_warn("No GPE found for gmux\n");
		sc->gpe = -1;
	}

	apple_gmux_softc_data = sc;
	init_completion(&sc->powerchange_done);
	gmux_enable_interrupts(sc);
	gmux_read_switch_state(sc);

	/*
	 * Retina MacBook Pros cannot switch the panel's AUX separately
	 * and need eDP pre-calibration. They are distinguishable from
	 * pre-retinas by having an "indexed" gmux.
	 *
	 * Pre-retina MacBook Pros can switch the panel's DDC separately.
	 */
	if (sc->indexed)
		ret = vga_switcheroo_register_handler(&gmux_handler_indexed,
					      VGA_SWITCHEROO_NEEDS_EDP_CONFIG);
	else
		ret = vga_switcheroo_register_handler(&gmux_handler_classic,
					      VGA_SWITCHEROO_CAN_SWITCH_DDC);
	if (ret) {
		pr_err("Failed to register vga_switcheroo handler\n");
		goto err_register_handler;
	}

	return 0;

err_register_handler:
	gmux_disable_interrupts(sc);
	apple_gmux_softc_data = NULL;
	if (sc->gpe >= 0)
		AcpiDisableGpe(NULL, sc->gpe);
err_enable_gpe:
	if (sc->gpe >= 0)
		AcpiRemoveNotifyHandler(sc->dhandle, ACPI_DEVICE_NOTIFY,
					gmux_notify_handler);
err_notify:
	/* backlight_device_unregister(bdev); */
err_release:
	drm_fini_pdev(&sc->pdev);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->io_rid, sc->io_res);

	return(ret);
}

static int
apple_gmux_detach(device_t dev)
{
	struct apple_gmux_softc *sc = device_get_softc(dev);

	vga_switcheroo_unregister_handler();
	gmux_disable_interrupts(sc);
	if (sc->gpe >= 0) {
		AcpiDisableGpe(NULL, sc->gpe);
		AcpiRemoveNotifyHandler(sc->dhandle, ACPI_DEVICE_NOTIFY,
					gmux_notify_handler);
	}

	apple_gmux_softc_data = NULL;
	drm_fini_pdev(&sc->pdev);
	return(bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->io_rid, sc->io_res));
}

static device_method_t apple_gmux_methods[] = {
	DEVMETHOD(device_probe,		apple_gmux_probe),
	DEVMETHOD(device_attach,	apple_gmux_attach),
	DEVMETHOD(device_detach,	apple_gmux_detach),
	DEVMETHOD(device_resume,	apple_gmux_resume),
	DEVMETHOD(device_suspend,	apple_gmux_suspend),

	DEVMETHOD_END
};

devclass_t gmux_devclass;

static driver_t apple_gmux_driver = {
	"apple_gmux",
	apple_gmux_methods,
	sizeof(struct apple_gmux_softc)
};

DRIVER_MODULE(gmux, acpi, apple_gmux_driver, gmux_devclass, 0, 0);
MODULE_VERSION(apple_gmux, 1);
MODULE_DEPEND(apple_gmux, drm, 1, 1, 2);
MODULE_DEPEND(apple_gmux, vga_switcheroo, 1, 1, 2);
