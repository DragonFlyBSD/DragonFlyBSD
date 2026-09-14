/*-
 * Copyright (c) 2000 Doug Rabson
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/stand/efi/libefi/libefi.c 350656 2019-08-06 20:13:28Z tsoome $
 */

#include <efi.h>
#include <efilib.h>

EFI_HANDLE		IH;
EFI_SYSTEM_TABLE	*ST;
EFI_BOOT_SERVICES	*BS;
EFI_RUNTIME_SERVICES	*RS;

void *
efi_get_table(EFI_GUID *tbl)
{
	EFI_GUID *id;
	int i;

	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		id = &ST->ConfigurationTable[i].VendorGuid;
		if (!memcmp(id, tbl, sizeof(EFI_GUID)))
			return (ST->ConfigurationTable[i].VendorTable);
	}
	return (NULL);
}

EFI_STATUS
OpenProtocolByHandle(EFI_HANDLE handle, EFI_GUID *protocol, void **interface)
{
	return (BS->OpenProtocol(handle, protocol, interface, IH, NULL,
	    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL));
}

/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 * Copyright (c) 2014 Nathan Whitehorn
 * All rights reserved.
 * Copyright (c) 2015 Eric McCorkle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD: head/sys/boot/efi/boot1/boot1.c 296713 2016-03-12 06:50:16Z andrew $
 */

/*
 * device_paths_match returns TRUE if the imgpath isn't NULL and all nodes
 * in imgpath and devpath match up to their respect occurrences of a media
 * node, FALSE otherwise.
 */
BOOLEAN
efi_device_paths_match(EFI_DEVICE_PATH *imgpath, EFI_DEVICE_PATH *devpath)
{
	size_t length;

	if (imgpath == NULL)
		return (FALSE);

	while (!IsDevicePathEnd(imgpath) && !IsDevicePathEnd(devpath)) {
		if (IsDevicePathType(imgpath, MEDIA_DEVICE_PATH) &&
		    IsDevicePathType(devpath, MEDIA_DEVICE_PATH))
			return (TRUE);

		length = DevicePathNodeLength(imgpath);
		if (imgpath->Type != devpath->Type ||
		    imgpath->SubType != devpath->SubType ||
		    length != DevicePathNodeLength(devpath) ||
		    memcmp(imgpath, devpath, length) != 0)
			return (FALSE);

		imgpath = NextDevicePathNode(imgpath);
		devpath = NextDevicePathNode(devpath);
	}

	return (FALSE);
}
