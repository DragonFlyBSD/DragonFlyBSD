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

/* Register GPIO device with ACPICA for ACPI-5.0 GPIO functionality */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/bus.h>

#include "opt_acpi.h"
#include "acpi.h"
#include <dev/acpica/acpivar.h>

#include "gpio_acpivar.h"

#include "gpio_if.h"

struct acpi_event_info {
	device_t dev;
	u_int pin;
	void *cookie;
	int trigger;
};

struct acpi_gpio_handler_data {
	struct acpi_connection_info info;
	device_t dev;
};

struct gpio_acpi_softc {
	device_t dev;
	device_t parent;
	struct acpi_event_info *infos;
	int num_aei;
	struct acpi_gpio_handler_data space_handler_data;
};

static int	gpio_acpi_probe(device_t dev);
static int	gpio_acpi_attach(device_t dev);
static int	gpio_acpi_detach(device_t dev);

static BOOLEAN	gpio_acpi_check_gpioint(device_t dev, ACPI_RESOURCE_GPIO *gpio);
static void	**gpioio_alloc_pins(device_t dev, device_t provider,
		    ACPI_RESOURCE_GPIO *gpio, uint16_t idx, uint16_t length,
		    void **buf);

/* GPIO Address Space Handler */
static void		gpio_acpi_install_address_space_handler(
			    struct gpio_acpi_softc *sc);
static void		gpio_acpi_remove_address_space_handler(
			    struct gpio_acpi_softc *sc);
static ACPI_STATUS	gpio_acpi_space_handler(UINT32 Function,
			    ACPI_PHYSICAL_ADDRESS Address, UINT32 BitWidth,
			    UINT64 *Value, void *HandlerContext,
			    void *RegionContext);

/* ACPI Event Interrupts */
static void	gpio_acpi_do_map_aei(struct gpio_acpi_softc *sc,
		    ACPI_RESOURCE_GPIO *gpio);
static void	gpio_acpi_map_aei(struct gpio_acpi_softc *sc);
static void	gpio_acpi_unmap_aei(struct gpio_acpi_softc *sc);
static void	gpio_acpi_handle_event(void *Context);
static void	gpio_acpi_aei_handler(void *arg);

/* Sanity-Check for GpioInt resources */
static BOOLEAN
gpio_acpi_check_gpioint(device_t dev, ACPI_RESOURCE_GPIO *gpio)
{
	if (gpio->PinTableLength != 1) {
		device_printf(dev,
		    "Unexepcted GpioInt resource PinTableLength %d\n",
		    gpio->PinTableLength);
		return (FALSE);
	}
	switch (gpio->Triggering) {
	case ACPI_LEVEL_SENSITIVE:
	case ACPI_EDGE_SENSITIVE:
		break;
	default:
		device_printf(dev, "Invalid GpioInt resource Triggering: %d\n",
		    gpio->Triggering);
		return (FALSE);
	}
	switch (gpio->Polarity) {
	case ACPI_ACTIVE_HIGH:
	case ACPI_ACTIVE_LOW:
	case ACPI_ACTIVE_BOTH:
		break;
	default:
		device_printf(dev, "Invalid GpioInt resource Polarity: %d\n",
		    gpio->Polarity);
		return (FALSE);
	}

	return (TRUE);
}

/*
 * GpioIo ACPI resource handling
 */

static void **
gpioio_alloc_pins(device_t dev, device_t provider, ACPI_RESOURCE_GPIO *gpio,
    uint16_t idx, uint16_t length, void **buf)
{
	void **pins;
	int flags, i, j;

	if (buf == NULL) {
		pins = kmalloc(sizeof(*pins) * length, M_DEVBUF,
		    M_WAITOK | M_ZERO);
	} else {
		pins = buf;
	}

	if (gpio->IoRestriction == ACPI_IO_RESTRICT_INPUT) {
		flags = (1U << 0);
	} else if (gpio->IoRestriction ==
	    ACPI_IO_RESTRICT_OUTPUT) {
		flags = (1U << 1);
	} else {
		flags = (1U << 0) | (1U << 1);
	}
	for (i = 0; i < length; i++) {
		if (GPIO_ALLOC_IO_PIN(provider, gpio->PinTable[idx + i], flags,
		    &pins[i]) != 0) {
			device_printf(dev, "Failed to alloc GpioIo pin %u on "
			    "ResourceSource \"%s\"\n", gpio->PinTable[idx + i],
			    gpio->ResourceSource.StringPtr);
			/* Release already alloc-ed pins */
			for (j = 0; j < i; j++)
				GPIO_RELEASE_IO_PIN(provider, pins[j]);
			goto err;
		}
	}

	return (pins);

err:
	if (buf == NULL)
		kfree(pins, M_DEVBUF);
	return (NULL);
}

/*
 * GPIO Address space handler
 */

static void
gpio_acpi_install_address_space_handler(struct gpio_acpi_softc *sc)
{
	struct acpi_gpio_handler_data *data = &sc->space_handler_data;
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(sc->parent);
	data->dev = sc->parent;
	s = AcpiInstallAddressSpaceHandler(handle, ACPI_ADR_SPACE_GPIO,
	    &gpio_acpi_space_handler, NULL, data);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->dev,
		    "Failed to install GPIO Address Space Handler in ACPI\n");
	}
}

static void
gpio_acpi_remove_address_space_handler(struct gpio_acpi_softc *sc)
{
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(sc->parent);
	s = AcpiRemoveAddressSpaceHandler(handle, ACPI_ADR_SPACE_GPIO,
	    &gpio_acpi_space_handler);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->dev,
		    "Failed to remove GPIO Address Space Handler from ACPI\n");
	}
}

static ACPI_STATUS
gpio_acpi_space_handler(UINT32 Function, ACPI_PHYSICAL_ADDRESS Address,
    UINT32 BitWidth, UINT64 *Value, void *HandlerContext, void *RegionContext)
{
	struct acpi_gpio_handler_data *data = HandlerContext;
	device_t dev = data->dev;
	struct acpi_connection_info *info = &data->info;
	struct acpi_resource_gpio *gpio;
	UINT64 val;
	UINT8 action = Function & ACPI_IO_MASK;
	ACPI_RESOURCE *Resource;
	ACPI_STATUS s = AE_OK;
	void **pins;
	int i;

	if (Value == NULL)
		return (AE_BAD_PARAMETER);

	/* XXX probably unnecessary */
	if (BitWidth == 0 || BitWidth > 64)
		return (AE_BAD_PARAMETER);

	s = AcpiBufferToResource(info->Connection, info->Length, &Resource);
	if (ACPI_FAILURE(s)) {
		device_printf(dev, "AcpiBufferToResource failed\n");
		return (s);
	}
	if (Resource->Type != ACPI_RESOURCE_TYPE_GPIO) {
		device_printf(dev, "Resource->Type is wrong\n");
		s = AE_BAD_PARAMETER;
		goto err;
	}
	gpio = &Resource->Data.Gpio;
	if (gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_IO) {
		device_printf(dev, "gpio->ConnectionType is wrong\n");
		s = AE_BAD_PARAMETER;
		goto err;
	}

	if (Address + BitWidth > gpio->PinTableLength) {
		device_printf(dev, "Address + BitWidth out of range\n");
		s = AE_BAD_ADDRESS;
		goto err;
	}

	if (gpio->IoRestriction == ACPI_IO_RESTRICT_OUTPUT &&
	    action == ACPI_READ) {
		device_printf(dev,
		    "IoRestriction is output only, but action is ACPI_READ\n");
		s = AE_BAD_PARAMETER;
		goto err;
	}
	if (gpio->IoRestriction == ACPI_IO_RESTRICT_INPUT &&
	    action == ACPI_WRITE) {
		device_printf(dev,
		    "IoRestriction is input only, but action is ACPI_WRITE\n");
		s = AE_BAD_PARAMETER;
		goto err;
	}

	/* Make sure we can access all pins, before trying actual read/write */
	pins = gpioio_alloc_pins(dev, dev, gpio, Address, BitWidth, NULL);
	if (pins == NULL) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	if (action == ACPI_READ) {
		*Value = 0;
		for (i = 0; i < BitWidth; i++) {
			val = GPIO_READ_PIN(dev, pins[i]);
			*Value |= val << i;
		}
	} else {
		for (i = 0; i < BitWidth; i++) {
			GPIO_WRITE_PIN(dev, pins[i],
			    (*Value & (1ULL << i)) ? 1 : 0);
		}
	}
	for (i = 0; i < BitWidth; i++)
		GPIO_RELEASE_IO_PIN(dev, pins[i]);
	kfree(pins, M_DEVBUF);

err:
	ACPI_FREE(Resource);
	return (s);
}

/*
 * ACPI Event Interrupts
 */

static void
gpio_acpi_handle_event(void *Context)
{
	struct acpi_event_info *info = (struct acpi_event_info *)Context;
	ACPI_HANDLE handle, h;
	ACPI_STATUS s;
	char buf[5];

	handle = acpi_get_handle(device_get_parent(info->dev));
	if (info->trigger == ACPI_EDGE_SENSITIVE) {
		ksnprintf(buf, sizeof(buf), "_E%02X", info->pin);
	} else {
		ksnprintf(buf, sizeof(buf), "_L%02X", info->pin);
	}
	if (info->pin <= 255 && ACPI_SUCCESS(AcpiGetHandle(handle, buf, &h))) {
		s = AcpiEvaluateObject(handle, buf, NULL, NULL);
		if (ACPI_FAILURE(s))
			device_printf(info->dev, "evaluating %s failed\n", buf);
	} else {
		ACPI_OBJECT_LIST arglist;
		ACPI_OBJECT arg;

		arglist.Pointer = &arg;
		arglist.Count = 1;
		arg.Type = ACPI_TYPE_INTEGER;
		arg.Integer.Value = info->pin;
		s = AcpiEvaluateObject(handle, "_EVT", &arglist, NULL);
		if (ACPI_FAILURE(s))
			device_printf(info->dev, "evaluating _EVT failed\n");
	}
}

static void
gpio_acpi_aei_handler(void *arg)
{
	struct acpi_event_info *info = (struct acpi_event_info *)arg;
	ACPI_STATUS s;

	s = AcpiOsExecute(OSL_GPE_HANDLER, gpio_acpi_handle_event, arg);
	if (ACPI_FAILURE(s)) {
		device_printf(info->dev,
		    "AcpiOsExecute for Acpi Event handler failed\n");
	}
}

static void
gpio_acpi_do_map_aei(struct gpio_acpi_softc *sc, ACPI_RESOURCE_GPIO *gpio)
{
	struct acpi_event_info *info = &sc->infos[sc->num_aei];
	uint16_t pin;
	void *cookie;

	if (gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_INT) {
		device_printf(sc->dev, "Unexpected gpio type %d\n",
		    gpio->ConnectionType);
		return;
	}

	/* sc->dev is correct here, since it's only used for device_printf */
	if (!gpio_acpi_check_gpioint(sc->dev, gpio))
		return;

	pin = gpio->PinTable[0];

	if (GPIO_ALLOC_INTR(sc->parent, pin, gpio->Triggering, gpio->Polarity,
	    gpio->PinConfig, &cookie) != 0) {
		device_printf(sc->dev,
		    "Failed to allocate AEI interrupt on pin %d\n", pin);
		return;
	}

	info->dev = sc->dev;
	info->pin = pin;
	info->trigger = gpio->Triggering;
	info->cookie = cookie;

	GPIO_SETUP_INTR(sc->parent, cookie, info, gpio_acpi_aei_handler);
	sc->num_aei++;
}

/* Map ACPI events */
static void
gpio_acpi_map_aei(struct gpio_acpi_softc *sc)
{
	ACPI_HANDLE handle = acpi_get_handle(sc->parent);
	ACPI_RESOURCE_GPIO *gpio;
	ACPI_RESOURCE *res, *end;
	ACPI_BUFFER b;
	ACPI_STATUS s;
	int n;

	sc->infos = NULL;
	sc->num_aei = 0;

	b.Pointer = NULL;
	b.Length = ACPI_ALLOCATE_BUFFER;
	s = AcpiGetEventResources(handle, &b);
	if (ACPI_FAILURE(s))
		return;

	end = (ACPI_RESOURCE *)((char *)b.Pointer + b.Length);
	/* Count Gpio connections */
	n = 0;
	for (res = (ACPI_RESOURCE *)b.Pointer; res < end;
	    res = ACPI_NEXT_RESOURCE(res)) {
		if (res->Type == ACPI_RESOURCE_TYPE_END_TAG) {
			break;
		} else if (res->Type == ACPI_RESOURCE_TYPE_GPIO) {
			n++;
		} else {
			device_printf(sc->dev, "Unexpected resource type %d\n",
			    res->Type);
		}
	}
	if (n <= 0) {
		AcpiOsFree(b.Pointer);
		return;
	}
	sc->infos = kmalloc(n*sizeof(*sc->infos), M_DEVBUF, M_WAITOK | M_ZERO);
	for (res = (ACPI_RESOURCE *)b.Pointer; res < end && sc->num_aei < n;
	    res = ACPI_NEXT_RESOURCE(res)) {
		if (res->Type == ACPI_RESOURCE_TYPE_END_TAG)
			break;
		if (res->Type == ACPI_RESOURCE_TYPE_GPIO) {
			gpio = (ACPI_RESOURCE_GPIO *)&res->Data;
			gpio_acpi_do_map_aei(sc, gpio);
		}
	}
	AcpiOsFree(b.Pointer);
}

/*  Unmap ACPI events */
static void
gpio_acpi_unmap_aei(struct gpio_acpi_softc *sc)
{
	struct acpi_event_info *info;
	int i;

	for (i = 0; i < sc->num_aei; i++) {
		info = &sc->infos[i];
		KKASSERT(info->dev != NULL);
		GPIO_TEARDOWN_INTR(sc->parent, info->cookie);
		GPIO_FREE_INTR(sc->parent, info->cookie);
		/* XXX Wait until ACPI Event handler has finished */
		memset(info, 0, sizeof(*info));
	}
	kfree(sc->infos, M_DEVBUF);
	sc->infos = NULL;
	sc->num_aei = 0;
}

static int
gpio_acpi_probe(device_t dev)
{
	if (acpi_get_handle(device_get_parent(dev)) == NULL)
		return (ENXIO);

	device_set_desc(dev, "ACPI GeneralPurposeIo backend");

	return (0);
}

static int
gpio_acpi_attach(device_t dev)
{
	struct gpio_acpi_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	sc->parent = device_get_parent(dev);

	gpio_acpi_install_address_space_handler(sc);

	gpio_acpi_map_aei(sc);

	return (0);
}

static int
gpio_acpi_detach(device_t dev)
{
	struct gpio_acpi_softc *sc = device_get_softc(dev);

	if (sc->infos != NULL)
		gpio_acpi_unmap_aei(sc);

	gpio_acpi_remove_address_space_handler(sc);

	return (0);
}


static device_method_t gpio_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, gpio_acpi_probe),
	DEVMETHOD(device_attach, gpio_acpi_attach),
	DEVMETHOD(device_detach, gpio_acpi_detach),

	DEVMETHOD_END
};

static driver_t gpio_acpi_driver = {
	"gpio_acpi",
	gpio_acpi_methods,
	sizeof(struct gpio_acpi_softc)
};

static devclass_t gpio_acpi_devclass;

DRIVER_MODULE(gpio_acpi, gpio_intel, gpio_acpi_driver, gpio_acpi_devclass,
    NULL, NULL);
MODULE_DEPEND(gpio_acpi, acpi, 1, 1, 1);
MODULE_VERSION(gpio_acpi, 1);
