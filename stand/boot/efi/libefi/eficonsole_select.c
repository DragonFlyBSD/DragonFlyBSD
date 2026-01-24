/*
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stand.h>

#include <efi.h>
#include <efilib.h>

#include <Protocol/DevicePath.h>

static EFI_GUID global = { 0x8be4df61, 0x93ca, 0x11d2,
    { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };

#define	CONSOLE_SERIAL	1
#define	CONSOLE_VIDEO	2

static EFI_STATUS
efi_global_getenv(const char *name, void *data, size_t *sz)
{
	CHAR16 *var;
	EFI_STATUS status;
	UINTN datasz;

	var = NULL;
	var = malloc((strlen(name) + 1) * sizeof(*var));
	if (var == NULL)
		return (EFI_OUT_OF_RESOURCES);
	for (size_t i = 0; i < strlen(name); i++)
		var[i] = (unsigned char)name[i];
	var[strlen(name)] = 0;

	datasz = (UINTN)*sz;
	status = RS->GetVariable(var, &global, NULL, &datasz, data);
	free(var);
	*sz = (size_t)datasz;
	return (status);
}

static int
efi_scan_consoles(const void *buf, size_t sz)
{
	const char *ep = (const char *)buf + sz;
	EFI_DEVICE_PATH *node = (EFI_DEVICE_PATH *)(void *)buf;
	int seen_serial = 0;
	int seen_video = 0;

	while ((char *)node < ep) {
		if (DevicePathType(node) == ACPI_DEVICE_PATH &&
		    (DevicePathSubType(node) == ACPI_DP ||
		    DevicePathSubType(node) == ACPI_EXTENDED_DP)) {
			ACPI_HID_DEVICE_PATH *acpi = (ACPI_HID_DEVICE_PATH *)node;
			if (EISA_ID_TO_NUM(acpi->HID) == 0x501)
				seen_serial = 1;
		}
		if (DevicePathType(node) == MESSAGING_DEVICE_PATH &&
		    DevicePathSubType(node) == MSG_UART_DP)
			seen_serial = 1;
		if (DevicePathType(node) == ACPI_DEVICE_PATH &&
		    DevicePathSubType(node) == ACPI_ADR_DP)
			seen_video = 1;
		node = NextDevicePathNode(node);
	}

	if (seen_serial)
		return (CONSOLE_SERIAL | (seen_video ? CONSOLE_VIDEO : 0));
	if (seen_video)
		return (CONSOLE_VIDEO);
	return (0);
}

int
efi_console_preferred(void)
{
	char buf[4096];
	size_t sz;
	int rv;

	sz = sizeof(buf);
	rv = efi_global_getenv("ConOut", buf, &sz);
	if (rv != EFI_SUCCESS) {
		sz = sizeof(buf);
		rv = efi_global_getenv("ConOutDev", buf, &sz);
	}
	if (rv != EFI_SUCCESS) {
		sz = sizeof(buf);
		rv = efi_global_getenv("ConIn", buf, &sz);
	}
	if (rv != EFI_SUCCESS)
		return (0);

	return (efi_scan_consoles(buf, sz));
}
