/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Apple DTGP / _DSM device property extraction.
 *
 * Walks the ACPI namespace looking for devices with _DSM methods,
 * evaluates them with Apple's DTGP UUID, and parses the returned
 * key-value property packages.
 */

#include <sys/uuid.h>

#include "apple_fw.h"

#define APPLE_DTGP_UUID_STRING	"a0b5b7c6-1318-441c-b0c9-fe695eaf949b"

static uint8_t apple_dtgp_uuid[ACPI_UUID_LENGTH];
static int apple_dtgp_uuid_ready;

static void
apple_fw_init_uuid(void)
{
	struct uuid uuid;

	if (apple_dtgp_uuid_ready)
		return;
	if (parse_uuid(APPLE_DTGP_UUID_STRING, &uuid) != 0)
		return;
	le_uuid_enc(apple_dtgp_uuid, &uuid);
	apple_dtgp_uuid_ready = 1;
}

/*
 * Evaluate _DSM on the given handle with Apple's DTGP UUID.
 * func=0: query supported functions (returns buffer with bitmask)
 * func=1: get device properties (returns package of key/value pairs)
 */
static ACPI_STATUS
apple_fw_eval_dsm(ACPI_HANDLE handle, int func, ACPI_BUFFER *result)
{
	ACPI_OBJECT args[4];
	ACPI_OBJECT_LIST arg_list;

	args[0].Type = ACPI_TYPE_BUFFER;
	args[0].Buffer.Length = ACPI_UUID_LENGTH;
	args[0].Buffer.Pointer = apple_dtgp_uuid;

	args[1].Type = ACPI_TYPE_INTEGER;
	args[1].Integer.Value = 1;	/* Revision */

	args[2].Type = ACPI_TYPE_INTEGER;
	args[2].Integer.Value = func;

	args[3].Type = ACPI_TYPE_PACKAGE;
	args[3].Package.Count = 0;
	args[3].Package.Elements = NULL;

	arg_list.Count = 4;
	arg_list.Pointer = args;

	result->Length = ACPI_ALLOCATE_BUFFER;
	result->Pointer = NULL;

	return (AcpiEvaluateObject(handle, "_DSM", &arg_list, result));
}

/*
 * Parse a package returned by Apple _DSM.
 * Format: alternating key (String), value (Integer/String/Buffer) pairs.
 */
static void
apple_fw_parse_dsm_props(device_t dev, struct apple_fw_dsm_dev *ddev,
    ACPI_OBJECT *pkg)
{
	ACPI_OBJECT *elem;
	uint32_t i;
	int n;

	if (pkg->Type != ACPI_TYPE_PACKAGE)
		return;

	for (i = 0; i + 1 < pkg->Package.Count; i += 2) {
		if (ddev->prop_count >= APPLE_FW_MAX_PROPS)
			break;

		elem = &pkg->Package.Elements[i];
		if (elem->Type != ACPI_TYPE_STRING)
			continue;

		n = ddev->prop_count;
		strlcpy(ddev->props[n].key, elem->String.Pointer,
		    sizeof(ddev->props[n].key));

		elem = &pkg->Package.Elements[i + 1];
		switch (elem->Type) {
		case ACPI_TYPE_INTEGER:
			ddev->props[n].val_type = APPLE_FW_PROP_INT;
			ddev->props[n].ival = (int64_t)elem->Integer.Value;
			ddev->props[n].val_len = sizeof(int64_t);
			break;
		case ACPI_TYPE_STRING:
			ddev->props[n].val_type = APPLE_FW_PROP_STRING;
			strlcpy(ddev->props[n].sval, elem->String.Pointer,
			    sizeof(ddev->props[n].sval));
			ddev->props[n].val_len = elem->String.Length;
			break;
		case ACPI_TYPE_BUFFER:
			ddev->props[n].val_type = APPLE_FW_PROP_BUFFER;
			ddev->props[n].val_len =
			    MIN(elem->Buffer.Length, APPLE_FW_MAX_PROPVAL);
			memcpy(ddev->props[n].bval, elem->Buffer.Pointer,
			    ddev->props[n].val_len);
			break;
		default:
			continue;
		}
		ddev->prop_count++;
	}
}

/*
 * Namespace walk callback: check each device for _DSM with DTGP UUID.
 */
static ACPI_STATUS
apple_fw_dsm_walk_cb(ACPI_HANDLE handle, UINT32 level, void *context,
    void **retval)
{
	device_t dev = (device_t)context;
	struct apple_fw_softc *sc = device_get_softc(dev);
	struct apple_fw_dsm_dev *ddev;
	ACPI_HANDLE dsm_handle;
	ACPI_STATUS status;
	ACPI_BUFFER result;
	ACPI_BUFFER pathbuf;
	ACPI_OBJECT *obj;
	ACPI_DEVICE_INFO *info;
	char namebuf[256];

	/* Check if this device has a _DSM method */
	status = AcpiGetHandle(handle, "_DSM", &dsm_handle);
	if (ACPI_FAILURE(status))
		return (AE_OK);	/* Skip, keep walking */

	/* Try evaluating _DSM with DTGP UUID, function 1 */
	status = apple_fw_eval_dsm(handle, 1, &result);
	if (ACPI_FAILURE(status) || result.Pointer == NULL)
		return (AE_OK);

	obj = (ACPI_OBJECT *)result.Pointer;

	/*
	 * Apple _DSM returns either a Package (properties) or Integer
	 * (error/not-supported).  Only packages contain useful data.
	 */
	if (!ACPI_PKG_VALID(obj, 2)) {
		AcpiOsFree(result.Pointer);
		return (AE_OK);
	}

	if (sc->sc_dsm_count >= APPLE_FW_MAX_DSM_DEVS) {
		AcpiOsFree(result.Pointer);
		return (AE_OK);
	}

	ddev = &sc->sc_dsm_devs[sc->sc_dsm_count];
	memset(ddev, 0, sizeof(*ddev));

	/* Get full ACPI path */
	pathbuf.Length = sizeof(namebuf);
	pathbuf.Pointer = namebuf;
	status = AcpiGetName(handle, ACPI_FULL_PATHNAME, &pathbuf);
	if (ACPI_SUCCESS(status))
		strlcpy(ddev->path, namebuf, sizeof(ddev->path));

	/* Get short name from device info */
	status = AcpiGetObjectInfo(handle, &info);
	if (ACPI_SUCCESS(status)) {
		if (info->Valid & ACPI_VALID_HID)
			strlcpy(ddev->name, info->HardwareId.String,
			    sizeof(ddev->name));
		AcpiOsFree(info);
	}

	/* If no HID, use the ACPI single-segment name */
	if (ddev->name[0] == '\0') {
		ACPI_BUFFER nbuf;
		char snbuf[8];

		nbuf.Length = sizeof(snbuf);
		nbuf.Pointer = snbuf;
		if (ACPI_SUCCESS(AcpiGetName(handle, ACPI_SINGLE_NAME, &nbuf)))
			strlcpy(ddev->name, snbuf, sizeof(ddev->name));
	}

	/* Parse the property package */
	apple_fw_parse_dsm_props(dev, ddev, obj);
	AcpiOsFree(result.Pointer);

	if (ddev->prop_count > 0)
		sc->sc_dsm_count++;

	return (AE_OK);
}

void
apple_fw_discover_dsm_devices(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);

	sc->sc_dsm_count = 0;

	apple_fw_init_uuid();

	AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, 100,
	    apple_fw_dsm_walk_cb, NULL, dev, NULL);
}
