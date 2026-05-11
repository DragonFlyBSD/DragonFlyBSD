/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Apple EFI Firmware Properties driver for DragonFly BSD.
 * Discovers and exposes Apple-specific ACPI structures:
 * custom SSDTs, DTGP device properties, and OSDW state.
 */

#ifndef _DEV_APPLE_FW_APPLE_FW_H_
#define _DEV_APPLE_FW_APPLE_FW_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <contrib/dev/acpica/source/include/acpi.h>
#include <contrib/dev/acpica/source/include/accommon.h>

#include <dev/acpica/acpivar.h>

MALLOC_DECLARE(M_APPLEFW);

#define APPLE_FW_MAX_SSDTS	32
#define APPLE_FW_MAX_DSM_DEVS	64
#define APPLE_FW_MAX_PROPS	16
#define APPLE_FW_MAX_PROPKEY	64
#define APPLE_FW_MAX_PROPVAL	256

enum apple_fw_prop_type {
	APPLE_FW_PROP_INT = 0,
	APPLE_FW_PROP_STRING,
	APPLE_FW_PROP_BUFFER
};

struct apple_fw_dsm_prop {
	char			key[APPLE_FW_MAX_PROPKEY];
	union {
		int64_t		ival;
		char		sval[APPLE_FW_MAX_PROPVAL];
		uint8_t		bval[APPLE_FW_MAX_PROPVAL];
	};
	uint32_t		val_len;
	enum apple_fw_prop_type	val_type;
};

struct apple_fw_dsm_dev {
	char			path[128];
	char			name[16];
	int			prop_count;
	struct apple_fw_dsm_prop props[APPLE_FW_MAX_PROPS];
};

struct apple_fw_ssdt_entry {
	char			oem_table_id[ACPI_OEM_TABLE_ID_SIZE + 1];
};

struct apple_fw_softc {
	device_t		sc_dev;
	struct lock		sc_lock;

	/* OSDW state: 1 = macOS detected, 0 = other OS */
	int			sc_osdw;

	/* Apple SSDTs */
	int			sc_ssdt_count;
	struct apple_fw_ssdt_entry sc_ssdts[APPLE_FW_MAX_SSDTS];

	/* DSM device properties */
	int			sc_dsm_count;
	struct apple_fw_dsm_dev	sc_dsm_devs[APPLE_FW_MAX_DSM_DEVS];
};

/* apple_fw_dsm.c */
void	apple_fw_discover_dsm_devices(device_t dev);

/* apple_fw_sysctl.c */
void	apple_fw_sysctl_init(device_t dev);

#endif /* !_DEV_APPLE_FW_APPLE_FW_H_ */
