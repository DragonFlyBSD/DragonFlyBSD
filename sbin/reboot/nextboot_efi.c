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
#include <sys/efiio.h>
#include <sys/kenv.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/uuid.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kenv.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nextboot_efi.h"

#define EFI_VARIABLE_NON_VOLATILE	0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS	0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS	0x00000004
#define NEXTBOOT_EFI_ATTRS	(EFI_VARIABLE_NON_VOLATILE |		\
				 EFI_VARIABLE_BOOTSERVICE_ACCESS |	\
				 EFI_VARIABLE_RUNTIME_ACCESS)
#define NEXTBOOT_EFI_PATH	"/dev/efi"

static int efi_var_usable(int);

static const char nextboot_name[] = "NextBootConf";
static const struct uuid nextboot_guid = {
	.time_low = 0xc185c676,
	.time_mid = 0x2cc3,
	.time_hi_and_version = 0x4582,
	.clock_seq_hi_and_reserved = 0xbc,
	.clock_seq_low = 0xb8,
	.node = { 0x63, 0xe1, 0xc4, 0x19, 0xe7, 0x66 },
};

static int
fail(int force, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!force)
		verrx(1, fmt, ap);
	vwarnx(fmt, ap);
	va_end(ap);
	return (-1);
}

static const char *
efi_strerror(int error)
{
	switch (error) {
	case EINVAL:
		return ("the firmware rejected it as too large or invalid");
	case EAGAIN:
		return ("the firmware variable store is full; it is usually "
		    "cleaned up at the next boot");
	case ENOSPC:
		return ("the firmware variable store is full");
	case EROFS:
		return ("the firmware reports the variable as write-protected");
	case EACCES:
		return ("the firmware denied access");
	case ENOSYS:
		return ("the firmware does not support variable writes at "
		    "run time");
	case EIO:
		return ("the firmware reported a device error");
	case EDOOFUS:
		return ("the firmware refused it for security reasons or with "
		    "an unknown status");
	default:
		return (strerror(error));
	}
}

int
booted_uefi(void)
{
	char buf[32];
	size_t len;

	len = sizeof(buf);
	if (sysctlbyname("machdep.bootmethod", buf, &len, NULL, 0) == 0)
		return (strcmp(buf, "UEFI") == 0);

	/* Kernels before machdep.bootmethod only have the memory map. */
	len = 0;
	if (sysctlbyname("machdep.efi_map", NULL, &len, NULL, 0) == 0)
		return (len > 0);
	return (0);
}

int
nextboot_backend_known(void)
{
	char buf[16];

	return (kenv(KENV_GET, "nextboot_backend", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "efivar") == 0);
}

static int
efi_open(int force)
{
	int error, fd, securelevel;
	size_t len;

	if (access(NEXTBOOT_EFI_PATH, F_OK) != 0 &&
	    modfind("efirt") < 0 && kldload("efirt") == -1) {
		error = errno;
		switch (error) {
		case EPERM:
			len = sizeof(securelevel);
			if (sysctlbyname("kern.securelevel", &securelevel,
			    &len, NULL, 0) != 0)
				securelevel = -1;
			return (fail(force, "cannot load efirt: securelevel "
			    "is %d; add efirt_load=\"YES\" to "
			    "/boot/loader.conf and reboot", securelevel));
		case ENOMEM:
			return (fail(force, "efirt cannot use the EFI runtime "
			    "services of this firmware (%s); nextboot is not "
			    "available on this system", strerror(error)));
		case ENOENT:
			return (fail(force, "efirt.ko not found; install it "
			    "or add efirt_load=\"YES\" to /boot/loader.conf"));
		default:
			return (fail(force, "kldload efirt: %s",
			    strerror(error)));
		}
	}

	fd = open(NEXTBOOT_EFI_PATH, O_RDWR);
	if (fd == -1) {
		error = errno;
		return (fail(force, "cannot access EFI variables: %s",
		    strerror(error)));
	}
	if (efi_var_usable(fd) == -1) {
		error = errno;
		close(fd);
		if (error == ENXIO || error == ENOSYS)
			return (fail(force, "efirt cannot use the EFI runtime "
			    "services of this firmware (%s); nextboot is not "
			    "available on this system", strerror(error)));
		return (fail(force, "cannot use the EFI runtime services: %s",
		    strerror(error)));
	}
	return (fd);
}

static void
efi_var_init(struct efi_var_ioc *ev, efi_char *name)
{
	size_t i;

	for (i = 0; i < sizeof(nextboot_name); ++i)
		name[i] = (unsigned char)nextboot_name[i];
	ev->name = name;
	ev->namesize = sizeof(nextboot_name) * sizeof(efi_char);
	ev->vendor = nextboot_guid;
	ev->attrib = 0;
	ev->data = NULL;
	ev->datasize = 0;
}

static int
efi_var_usable(int fd)
{
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;

	efi_var_init(&ev, name);
	if (ioctl(fd, EFIIOC_VAR_GET, &ev) == -1 && errno != ENOENT)
		return (-1);
	return (0);
}

int
nextboot_efi_read(char *buf, size_t bufsz, size_t *lenp, int force)
{
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;
	int error, fd;

	fd = efi_open(force);
	if (fd == -1)
		return (-1);
	efi_var_init(&ev, name);
	ev.data = buf;
	ev.datasize = bufsz;
	if (ioctl(fd, EFIIOC_VAR_GET, &ev) == -1) {
		error = errno;
		close(fd);
		if (error == ENOENT)
			return (1);
		return (fail(force, "cannot read EFI variable %s: %s",
		    nextboot_name, strerror(error)));
	}
	close(fd);
	if (ev.data == NULL) {
		if (!force)
			errx(1, "the existing entry is larger than %zu bytes; "
			    "use -D first", bufsz);
		warnx("the existing entry is larger than %zu bytes, "
		    "replacing it", bufsz);
		return (-2);
	}
	buf[ev.datasize] = '\0';
	*lenp = ev.datasize;
	return (0);
}

static int
efi_var_state(int fd, uint32_t *attrp, char *buf, size_t bufsz)
{
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;

	*attrp = 0;
	efi_var_init(&ev, name);
	if (ioctl(fd, EFIIOC_VAR_GET, &ev) == -1)
		return (errno == ENOENT ? 0 : -2);
	*attrp = ev.attrib;
	if (*attrp == 0) {
		efi_var_init(&ev, name);
		ev.data = buf;
		ev.datasize = bufsz;
		if (ioctl(fd, EFIIOC_VAR_GET, &ev) == -1)
			return (errno == ENOENT ? 0 : -2);
		*attrp = ev.attrib;
	}
	if (*attrp == 0)
		return (-1);
	return (*attrp == NEXTBOOT_EFI_ATTRS ? 1 : 2);
}

static int
efi_var_set(int fd, const char *data, size_t len)
{
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;

	efi_var_init(&ev, name);
	ev.attrib = NEXTBOOT_EFI_ATTRS;
	ev.data = (void *)(uintptr_t)data;
	ev.datasize = len;
	if (ioctl(fd, EFIIOC_VAR_SET, &ev) == -1)
		return (-1);
	return (0);
}

static int
efi_var_delete(int fd, int force, uint32_t attrib)
{
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;
	int error;

	efi_var_init(&ev, name);
	if (ioctl(fd, EFIIOC_VAR_SET, &ev) == 0)
		return (0);
	error = errno;
	if (error == ENOENT)
		return (0);
	if (error == EINVAL) {
		efi_char name2[sizeof(nextboot_name)];

		efi_var_init(&ev, name2);
		ev.attrib = attrib != 0 ? attrib : NEXTBOOT_EFI_ATTRS;
		if (ioctl(fd, EFIIOC_VAR_SET, &ev) == 0)
			return (0);
		error = errno;
		if (error == ENOENT)
			return (0);
		if (error == EINVAL)
			return (fail(force, "cannot remove EFI variable %s: "
			    "the firmware rejected the delete as invalid",
			    nextboot_name));
		return (fail(force, "cannot remove EFI variable %s: %s",
		    nextboot_name, efi_strerror(error)));
	}
	return (fail(force, "cannot remove EFI variable %s: %s",
	    nextboot_name, efi_strerror(error)));
}

int
nextboot_efi_write(const char *data, size_t len, int force)
{
	static char check[NEXTBOOT_MAX_SIZE + 1];
	efi_char name[sizeof(nextboot_name)];
	struct efi_var_ioc ev;
	int deleted = 0, error, fd, had, removed, state, stored = 0;
	uint32_t attrib;

	fd = efi_open(force);
	if (fd == -1)
		return (-1);

	state = efi_var_state(fd, &attrib, check, sizeof(check));
	if (state == -2) {
		error = errno;
		close(fd);
		return (fail(force, "cannot read EFI variable %s: %s",
		    nextboot_name, strerror(error)));
	}
	had = state != 0;
	if (state == 2) {
		if (efi_var_delete(fd, force, attrib) == -1) {
			close(fd);
			return (-1);
		}
		deleted = 1;
	}
	if (efi_var_set(fd, data, len) == 0)
		stored = 1;
	else
		error = errno;
	if (!stored) {
		if (deleted && had)
			warnx("the previous entry was removed and the new one "
			    "was not stored");
		close(fd);
		return (fail(force, "cannot store the entry in EFI "
		    "variable %s: %s (%zu bytes)", nextboot_name,
		    efi_strerror(error), len));
	}

	efi_var_init(&ev, name);
	ev.data = check;
	ev.datasize = sizeof(check);
	if (ioctl(fd, EFIIOC_VAR_GET, &ev) == -1 || ev.data == NULL ||
	    ev.datasize != len || memcmp(check, data, len) != 0) {
		removed = efi_var_delete(fd, 1, NEXTBOOT_EFI_ATTRS) == 0;
		close(fd);
		return (fail(force, "the firmware did not return the entry "
		    "unchanged; %s", removed ? "it has been removed" :
		    "it is still armed"));
	}
	close(fd);
	return (0);
}

int
nextboot_efi_clear(void)
{
	static char buf[NEXTBOOT_MAX_SIZE + 1];
	uint32_t attrib;
	int error, fd;

	fd = efi_open(1);
	if (fd == -1)
		return (-1);
	efi_var_state(fd, &attrib, buf, sizeof(buf));
	error = efi_var_delete(fd, 1, attrib);
	close(fd);
	return (error);
}
