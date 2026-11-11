/*
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
#include <stand.h>
#include <string.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"
#include "dloader.h"
#include "loader_efi.h"

static CHAR16 nextboot_var[] = L"NextBootConf";
static const char nextboot_src[] = "EFI variable NextBootConf";

#define	NEXTBOOT_EFI_ATTRS	(EFI_VARIABLE_NON_VOLATILE | \
				 EFI_VARIABLE_BOOTSERVICE_ACCESS | \
				 EFI_VARIABLE_RUNTIME_ACCESS)

static EFI_STATUS
nextboot_query(UINT32 *attrib, UINTN *datasz)
{
	EFI_GUID guid = DRAGONFLY_BOOT_VAR_GUID;
	EFI_STATUS status;
	char c;

	*datasz = 0;
	status = RS->GetVariable(nextboot_var, &guid, attrib, datasz, NULL);
	if (status != EFI_INVALID_PARAMETER)
		return (status);
	*datasz = sizeof(c);
	return (RS->GetVariable(nextboot_var, &guid, attrib, datasz, &c));
}

int
efi_nextboot_read(const char *base, char *buf, size_t bufsz, size_t *lenp,
    int *truncp, const char **srcp)
{
	EFI_GUID guid = DRAGONFLY_BOOT_VAR_GUID;
	EFI_STATUS status;
	UINTN datasz;
	UINT32 attrib = 0;
	char path[MAXPATHLEN];
	char *data;
	size_t len;

	if (nextboot_file_armed(base, path, sizeof(path)))
		printf("nextboot: ignoring %s (UEFI uses the EFI variable)\n",
		    path);

	status = nextboot_query(&attrib, &datasz);
	if (status == EFI_NOT_FOUND)
		return (ENOENT);
	if (datasz == 0 ||
	    (EFI_ERROR(status) && status != EFI_BUFFER_TOO_SMALL)) {
		printf("nextboot: cannot read %s: error %#llx\n", nextboot_src,
		    (unsigned long long)status);
		return (EIO);
	}

	data = malloc(datasz);
	if (data == NULL) {
		printf("nextboot: no memory for %s\n", nextboot_src);
		return (ENOMEM);
	}
	status = RS->GetVariable(nextboot_var, &guid, &attrib, &datasz, data);
	if (EFI_ERROR(status)) {
		printf("nextboot: cannot read %s: error %#llx\n", nextboot_src,
		    (unsigned long long)status);
		free(data);
		return (EIO);
	}

	len = datasz;
	*truncp = len > bufsz;
	if (len > bufsz)
		len = bufsz;
	memcpy(buf, data, len);
	buf[len] = '\0';
	free(data);
	*lenp = len;
	*srcp = nextboot_src;
	return (0);
}

int
efi_nextboot_consume(size_t len_first __unused)
{
	EFI_GUID guid = DRAGONFLY_BOOT_VAR_GUID;
	EFI_STATUS status;
	UINTN datasz;
	UINT32 attrib = 0, attrs;

	status = RS->SetVariable(nextboot_var, &guid, 0, 0, NULL);
	if (status == EFI_INVALID_PARAMETER) {
		EFI_STATUS status2;

		datasz = 0;
		status2 = nextboot_query(&attrib, &datasz);
		attrs = attrib != 0 ? attrib : NEXTBOOT_EFI_ATTRS;
		if (status2 == EFI_BUFFER_TOO_SMALL || status2 == EFI_SUCCESS)
			status = RS->SetVariable(nextboot_var, &guid, attrs,
			    0, NULL);
	}
	if (EFI_ERROR(status) && status != EFI_NOT_FOUND) {
		printf("nextboot: cannot clear %s: error %#llx, ignoring\n",
		    nextboot_src, (unsigned long long)status);
		return (EIO);
	}

	status = nextboot_query(&attrib, &datasz);
	if (status != EFI_NOT_FOUND) {
		if (status == EFI_SUCCESS || status == EFI_BUFFER_TOO_SMALL)
			printf("nextboot: cannot clear %s: the firmware still "
			    "reports it, ignoring\n", nextboot_src);
		else
			printf("nextboot: cannot clear %s: error %#llx, "
			    "ignoring\n", nextboot_src,
			    (unsigned long long)status);
		return (EIO);
	}
	return (0);
}
