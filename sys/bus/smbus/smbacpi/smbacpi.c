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

/* Register SMBUS device with ACPICA for ACPI-5.0 GPIO functionality */

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
#include <contrib/dev/acpica/source/include/amlcode.h>

#include <bus/smbus/smbconf.h>

#include "smbus_if.h"

struct gsb_buffer {
	UINT8 status;
	UINT8 len;
	UINT8 data[];
} __packed;

struct acpi_i2c_handler_data {
	struct acpi_connection_info info;
	device_t dev;
};

struct smbus_acpi_softc {
	device_t dev;
	device_t parent;
	struct acpi_i2c_handler_data space_handler_data;
};

static int	smbus_acpi_probe(device_t dev);
static int	smbus_acpi_attach(device_t dev);
static int	smbus_acpi_detach(device_t dev);

/* SMBUS Address Space Handler */
static void		smbus_acpi_install_address_space_handler(
			    struct smbus_acpi_softc *sc);
static void		smbus_acpi_remove_address_space_handler(
			    struct smbus_acpi_softc *sc);
static ACPI_STATUS	smbus_acpi_space_handler(UINT32 Function,
			    ACPI_PHYSICAL_ADDRESS Address, UINT32 BitWidth,
			    UINT64 *Value, void *HandlerContext,
			    void *RegionContext);

/*
 * SMBUS Address space handler
 */

static void
smbus_acpi_install_address_space_handler(struct smbus_acpi_softc *sc)
{
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(sc->parent);
	sc->space_handler_data.dev = sc->parent;
	s = AcpiInstallAddressSpaceHandler(handle, ACPI_ADR_SPACE_GSBUS,
	    &smbus_acpi_space_handler, NULL, &sc->space_handler_data);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->dev,
		    "Failed to install GSBUS Address Space Handler in ACPI\n");
	}
}

static void
smbus_acpi_remove_address_space_handler(struct smbus_acpi_softc *sc)
{
	ACPI_HANDLE handle;
	ACPI_STATUS s;

	handle = acpi_get_handle(sc->parent);
	s = AcpiRemoveAddressSpaceHandler(handle, ACPI_ADR_SPACE_GSBUS,
	    &smbus_acpi_space_handler);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->dev,
		    "Failed to remove GSBUS Address Space Handler from ACPI\n");
	}
}

static ACPI_STATUS
smbus_acpi_space_handler(UINT32 Function, ACPI_PHYSICAL_ADDRESS Address,
    UINT32 BitWidth, UINT64 *Value, void *HandlerContext, void *RegionContext)
{
	struct gsb_buffer *gsb = (struct gsb_buffer *)Value;
	struct acpi_i2c_handler_data *data = HandlerContext;
	device_t dev = data->dev;
	struct acpi_connection_info *info = &data->info;
	struct acpi_resource_i2c_serialbus *sb;
	ACPI_RESOURCE *Resource;
	UINT32 accessor_type = Function >> 16;
	UINT8 action = Function & ACPI_IO_MASK;
	ACPI_STATUS s = AE_OK;
	int cnt, val = 0;
	uint16_t *wdata;
	short word;
	char byte;
	char buf[32];
	u_char count;

	if (Value == NULL)
		return (AE_BAD_PARAMETER);

	s = AcpiBufferToResource(info->Connection, info->Length,
	    &Resource);
	if (ACPI_FAILURE(s))
		return s;
	if (Resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	sb = &Resource->Data.I2cSerialBus;
	if (sb->Type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	/* XXX Ignore 10bit addressing for now */
	if (sb->AccessMode == ACPI_I2C_10BIT_MODE) {
		s = AE_BAD_PARAMETER;
		goto err;
	}

	switch (accessor_type) {
	case AML_FIELD_ATTRIB_SEND_RCV:
		if (action == ACPI_READ) {
			val = SMBUS_RECVB(dev, sb->SlaveAddress, &byte);
			if (val == 0)
				gsb->data[0] = byte;
		} else {
			val = SMBUS_SENDB(dev, sb->SlaveAddress,
			    gsb->data[0]);
		}
		break;
	case AML_FIELD_ATTRIB_BYTE:
		if (action == ACPI_READ) {
			val = SMBUS_READB(dev, sb->SlaveAddress, Address,
			    &byte);
			if (val == 0)
				gsb->data[0] = byte;
		} else {
			val = SMBUS_WRITEB(dev, sb->SlaveAddress, Address,
			    gsb->data[0]);
		}
		break;
	case AML_FIELD_ATTRIB_WORD:
		wdata = (uint16_t *)gsb->data;
		if (action == ACPI_READ) {
			val = SMBUS_READW(dev, sb->SlaveAddress, Address,
			    &word);
			if (val == 0)
				wdata[0] = word;
		} else {
			val = SMBUS_WRITEW(dev, sb->SlaveAddress, Address,
			    wdata[0]);
		}
		break;
	case AML_FIELD_ATTRIB_BLOCK:
		if (action == ACPI_READ) {
			count = 32;
			val = SMBUS_BREAD(dev, sb->SlaveAddress, Address,
			    &count, buf);
			if (val == 0) {
				gsb->len = count;
				memcpy(gsb->data, buf, count);
			}
		} else {
			memcpy(buf, gsb->data, gsb->len);
			count = gsb->len;
			val = SMBUS_BWRITE(dev, sb->SlaveAddress, Address,
			    count, buf);
		}
		break;
	case AML_FIELD_ATTRIB_MULTIBYTE:
		if (action == ACPI_READ) {
			cnt = 0;
			val = SMBUS_TRANS(dev, sb->SlaveAddress, Address,
			    SMB_TRANS_NOCNT | SMB_TRANS_7BIT, NULL, 0,
			    buf, info->AccessLength, &cnt);
			if (val == 0)
				memcpy(gsb->data, buf, cnt);
		} else {
			memcpy(buf, gsb->data, info->AccessLength);
			val = SMBUS_TRANS(dev, sb->SlaveAddress, Address,
			    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
			    buf, info->AccessLength, NULL, 0, NULL);
		}
		break;
	default:
		device_printf(dev, "protocol(0x%02x) is not supported.\n",
		    accessor_type);
		s = AE_BAD_PARAMETER;
		goto err;
	}

	gsb->status = val;

err:
	ACPI_FREE(Resource);

	return (s);
}

static int
smbus_acpi_probe(device_t dev)
{
	if (acpi_get_handle(device_get_parent(dev)) == NULL)
		return (ENXIO);

	device_set_desc(dev, "ACPI I2cSerialBus backend");

	return (0);
}

static int
smbus_acpi_attach(device_t dev)
{
	struct smbus_acpi_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	sc->parent = device_get_parent(dev);

	smbus_acpi_install_address_space_handler(sc);

	return (0);
}

static int
smbus_acpi_detach(device_t dev)
{
	struct smbus_acpi_softc *sc = device_get_softc(dev);

	smbus_acpi_remove_address_space_handler(sc);

	return (0);
}

static device_method_t smbacpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, smbus_acpi_probe),
	DEVMETHOD(device_attach, smbus_acpi_attach),
	DEVMETHOD(device_detach, smbus_acpi_detach),

	DEVMETHOD_END
};

static driver_t smbacpi_driver = {
	"smbacpi",
	smbacpi_methods,
	sizeof(struct smbus_acpi_softc)
};

static devclass_t smbacpi_devclass;

DRIVER_MODULE(smbacpi, ig4iic, smbacpi_driver, smbacpi_devclass,
    NULL, NULL);
MODULE_DEPEND(smbacpi, acpi, 1, 1, 1);
MODULE_DEPEND(smbacpi, smbus, 1, 1, 1);
MODULE_VERSION(smbacpi, 1);
