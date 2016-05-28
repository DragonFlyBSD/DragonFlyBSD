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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
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

struct gpio_acpi_data {
	struct acpi_event_info *infos;
	int num_aei;
	struct acpi_gpio_handler_data space_handler_data;
};

static BOOLEAN	gpio_acpi_check_gpioint(device_t dev, ACPI_RESOURCE_GPIO *gpio);

/* GPIO Address Space Handler */
static void		gpio_acpi_install_address_space_handler(device_t dev,
			    struct acpi_gpio_handler_data *data);
static void		gpio_acpi_remove_address_space_handler(device_t dev,
			    struct acpi_gpio_handler_data *data);
static ACPI_STATUS	gpio_acpi_space_handler(UINT32 Function,
			    ACPI_PHYSICAL_ADDRESS Address, UINT32 BitWidth,
			    UINT64 *Value, void *HandlerContext,
			    void *RegionContext);

/* ACPI Event Interrupts */
static void	gpio_acpi_do_map_aei(device_t dev,
		    struct acpi_event_info *info, ACPI_RESOURCE_GPIO *gpio);
static void	*gpio_acpi_map_aei(device_t dev, int *num_aei);
static void	gpio_acpi_unmap_aei(device_t dev, struct gpio_acpi_data *data);
static void	gpio_acpi_handle_event(void *Context);
static void	gpio_acpi_aei_handler(void *arg);

/* Register GPIO device with ACPICA for ACPI-5.0 GPIO functionality */
void *
gpio_acpi_register(device_t dev)
{
	struct gpio_acpi_data *data;

	data = kmalloc(sizeof(*data), M_DEVBUF, M_WAITOK | M_ZERO);

	gpio_acpi_install_address_space_handler(dev,
	    &data->space_handler_data);

	data->infos = gpio_acpi_map_aei(dev, &data->num_aei);

	return data;
}

void
gpio_acpi_unregister(device_t dev, void *arg)
{
	struct gpio_acpi_data *data = (struct gpio_acpi_data *)arg;

	if (data->infos != NULL)
		gpio_acpi_unmap_aei(dev, data);

	gpio_acpi_remove_address_space_handler(dev, &data->space_handler_data);

	kfree(data, M_DEVBUF);
}

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
 * GPIO Address space handler
 */

static void
gpio_acpi_install_address_space_handler(device_t dev,
    struct acpi_gpio_handler_data *data)
{
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(dev);
	data->dev = dev;
	/*
	 * XXX Should use the Address Space Setup Handler to check and
	 *     reserve the GPIO pins, to avoid checking on each READ/WRITE
	 *     call into the Adress Space Handler.
	 */
	s = AcpiInstallAddressSpaceHandler(handle, ACPI_ADR_SPACE_GPIO,
	    &gpio_acpi_space_handler, NULL, data);
	if (ACPI_FAILURE(s)) {
		device_printf(dev,
		    "Failed to install GPIO Address Space Handler in ACPI\n");
	}
}

static void
gpio_acpi_remove_address_space_handler(device_t dev,
    struct acpi_gpio_handler_data *data)
{
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(dev);
	s = AcpiRemoveAddressSpaceHandler(handle, ACPI_ADR_SPACE_GPIO,
	    &gpio_acpi_space_handler);
	if (ACPI_FAILURE(s)) {
		device_printf(dev,
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
	int i;

	s = AcpiBufferToResource(info->Connection, info->Length, &Resource);
	if (ACPI_FAILURE(s))
		return (s);
	if (Value == NULL || Resource->Type != ACPI_RESOURCE_TYPE_GPIO) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	gpio = &Resource->Data.Gpio;
	if (gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_IO) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	/* XXX probably unnecessary */
	if (BitWidth == 0 || BitWidth > 64) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	if (Address + BitWidth > gpio->PinTableLength) {
		s = AE_BAD_ADDRESS;
		goto err;
	}

	if (action == ACPI_READ) {
		if (gpio->IoRestriction == ACPI_IO_RESTRICT_OUTPUT) {
			s = AE_BAD_PARAMETER;
			goto err;
		}
		*Value = 0;
		for (i = 0; i < BitWidth; i++) {
			val = GPIO_READ_PIN(dev, gpio->PinTable[Address + i]);
			*Value |= val << i;
		}
	} else {
		if (gpio->IoRestriction == ACPI_IO_RESTRICT_INPUT) {
			s = AE_BAD_PARAMETER;
			goto err;
		}
		for (i = 0; i < BitWidth; i++) {
			GPIO_WRITE_PIN(dev, gpio->PinTable[Address + i],
			    *Value & (1ULL << i) ? 1 : 0);
		}
	}

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

	handle = acpi_get_handle(info->dev);
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
gpio_acpi_do_map_aei(device_t dev, struct acpi_event_info *info,
    ACPI_RESOURCE_GPIO *gpio)
{
	uint16_t pin;
	void *cookie;

	if (gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_INT) {
		device_printf(dev, "Unexpected gpio type %d\n",
		    gpio->ConnectionType);
		return;
	}

	if (!gpio_acpi_check_gpioint(dev, gpio))
		return;

	pin = gpio->PinTable[0];

	if (GPIO_ALLOC_INTR(dev, pin, gpio->Triggering, gpio->Polarity,
	    gpio->PinConfig, &cookie) != 0) {
		device_printf(dev,
		    "Failed to allocate AEI interrupt on pin %d\n", pin);
		return;
	}

	info->dev = dev;
	info->pin = pin;
	info->trigger = gpio->Triggering;
	info->cookie = cookie;

	GPIO_SETUP_INTR(dev, cookie, info, gpio_acpi_aei_handler);
}

/* Map ACPI events */
static void *
gpio_acpi_map_aei(device_t dev, int *num_aei)
{
	ACPI_HANDLE handle = acpi_get_handle(dev);
	ACPI_RESOURCE_GPIO *gpio;
	ACPI_RESOURCE *res, *end;
	ACPI_BUFFER b;
	ACPI_STATUS s;
	struct acpi_event_info *infos = NULL;
	int n;

	*num_aei = 0;

	b.Pointer = NULL;
	b.Length = ACPI_ALLOCATE_BUFFER;
	s = AcpiGetEventResources(handle, &b);
	if (ACPI_SUCCESS(s)) {
		end = (ACPI_RESOURCE *)((char *)b.Pointer + b.Length);
		/* Count Gpio connections */
		n = 0;
		for (res = (ACPI_RESOURCE *)b.Pointer; res < end;
		    res = ACPI_NEXT_RESOURCE(res)) {
			if (res->Type == ACPI_RESOURCE_TYPE_END_TAG)
				break;
			switch (res->Type) {
			case ACPI_RESOURCE_TYPE_GPIO:
				n++;
				break;
			default:
				break;
			}
		}
		if (n <= 0) {
			AcpiOsFree(b.Pointer);
			return (infos);
		}
		infos = kmalloc(n*sizeof(*infos), M_DEVBUF, M_WAITOK | M_ZERO);
		*num_aei = n;
		n = 0;
		for (res = (ACPI_RESOURCE *)b.Pointer; res < end;
		    res = ACPI_NEXT_RESOURCE(res)) {
			if (res->Type == ACPI_RESOURCE_TYPE_END_TAG)
				break;
			switch (res->Type) {
			case ACPI_RESOURCE_TYPE_GPIO:
				gpio = (ACPI_RESOURCE_GPIO *)&res->Data;
				gpio_acpi_do_map_aei(dev, &infos[n++], gpio);
				break;
			default:
				device_printf(dev,
				    "Unexpected resource type %d\n",
				    res->Type);
				break;
			};
		}
		AcpiOsFree(b.Pointer);
	}
	return (infos);
}

/*  Unmap ACPI events */
static void
gpio_acpi_unmap_aei(device_t dev, struct gpio_acpi_data *data)
{
	struct acpi_event_info *info;
	int i;

	for (i = 0; i < data->num_aei; i++) {
		info = &data->infos[i];
		if (info->dev != NULL) {
			GPIO_TEARDOWN_INTR(dev, info->cookie);
			GPIO_FREE_INTR(dev, info->cookie);
			/* XXX Wait until ACPI Event handler has finished */
			memset(info, 0, sizeof(*info));
		}
	}
	kfree(data->infos, M_DEVBUF);
}

MODULE_DEPEND(gpio_acpi, acpi, 1, 1, 1);
MODULE_VERSION(gpio_acpi, 1);
