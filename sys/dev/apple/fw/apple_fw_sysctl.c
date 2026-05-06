/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Sysctl tree for apple_fw(4).
 *
 * Exposes Apple EFI firmware metadata under dev.apple_fw.0.*
 */

#include <sys/ctype.h>

#include "apple_fw.h"

void
apple_fw_sysctl_init(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid *ssdt_tree;
	struct sysctl_oid *dsm_tree, *dev_node;
	struct apple_fw_dsm_dev *ddev;
	struct apple_fw_dsm_prop *prop;
	char numbuf[8];
	int i, j;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	/* OSDW */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "osdw", CTLFLAG_RD, &sc->sc_osdw, 0,
	    "OSDW: 1 if firmware detects macOS, 0 otherwise");

	/* SSDT count */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "ssdt_count", CTLFLAG_RD, &sc->sc_ssdt_count, 0,
	    "Number of Apple SSDTs");

	/* SSDT names */
	ssdt_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "ssdt", CTLFLAG_RD, NULL, "Apple SSDT tables");

	for (i = 0; i < sc->sc_ssdt_count; i++) {
		ksnprintf(numbuf, sizeof(numbuf), "%d", i);
		SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(ssdt_tree),
		    OID_AUTO, numbuf, CTLFLAG_RD,
		    sc->sc_ssdts[i].oem_table_id, 0,
		    "OEM Table ID");
	}

	/* DSM device count */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "dsm_count", CTLFLAG_RD, &sc->sc_dsm_count, 0,
	    "Number of devices with Apple _DSM properties");

	/* DSM device properties */
	dsm_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "dsm", CTLFLAG_RD, NULL, "Apple _DSM device properties");

	for (i = 0; i < sc->sc_dsm_count; i++) {
		ddev = &sc->sc_dsm_devs[i];

		ksnprintf(numbuf, sizeof(numbuf), "%d", i);
		dev_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(dsm_tree),
		    OID_AUTO, numbuf, CTLFLAG_RD, NULL, ddev->path);

		SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(dev_node), OID_AUTO,
		    "name", CTLFLAG_RD, ddev->name, 0,
		    "ACPI device name");

		SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(dev_node), OID_AUTO,
		    "path", CTLFLAG_RD, ddev->path, 0,
		    "Full ACPI path");

		for (j = 0; j < ddev->prop_count; j++) {
			prop = &ddev->props[j];

			switch (prop->val_type) {
			case APPLE_FW_PROP_INT:
				SYSCTL_ADD_QUAD(ctx,
				    SYSCTL_CHILDREN(dev_node), OID_AUTO,
				    prop->key, CTLFLAG_RD,
				    &prop->ival, 0,
				    "Device property (integer)");
				break;
			case APPLE_FW_PROP_STRING:
				SYSCTL_ADD_STRING(ctx,
				    SYSCTL_CHILDREN(dev_node), OID_AUTO,
				    prop->key, CTLFLAG_RD,
				    prop->sval, 0,
				    "Device property (string)");
				break;
			case APPLE_FW_PROP_BUFFER:
			    {
				int is_str = 0;
				if (prop->val_len > 1 &&
				    prop->bval[prop->val_len - 1] == '\0') {
					uint32_t k;
					is_str = 1;
					for (k = 0; k < prop->val_len - 1; k++) {
						if (!isprint(prop->bval[k])) {
							is_str = 0;
							break;
						}
					}
				}
				if (is_str) {
					SYSCTL_ADD_STRING(ctx,
					    SYSCTL_CHILDREN(dev_node),
					    OID_AUTO, prop->key, CTLFLAG_RD,
					    (char *)prop->bval, 0,
					    "Device property (string)");
				} else {
					SYSCTL_ADD_OPAQUE(ctx,
					    SYSCTL_CHILDREN(dev_node),
					    OID_AUTO, prop->key, CTLFLAG_RD,
					    prop->bval, prop->val_len,
					    "IU", "Device property (buffer)");
				}
			    }
				break;
			}
		}
	}
}
