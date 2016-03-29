/*
 * Copyright (c) 2015 Rimvydas Jasinskas
 *
 * Simple EDID firmware handling routines derived from
 * drm_edid.c:drm_do_get_edid()
 *
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <sys/bus.h>
#include <sys/firmware.h>

#ifdef CONFIG_DRM_LOAD_EDID_FIRMWARE
static u8 *do_edid_fw_load(struct drm_connector *connector, const char *fwname,
			   const char *connector_name);

static char edidfw_tun[256];
TUNABLE_STR("drm.edid_firmware", edidfw_tun, sizeof(edidfw_tun));

int drm_load_edid_firmware(struct drm_connector *connector)
{
	const char *connector_name = connector->name;
	char *cp, *fwname = edidfw_tun;
	struct edid *fwedid;
	int ret;

	if (*fwname == '\0')
		return 0;

	/* Check for connector specifier presence */
	if ((cp = strchr(fwname, ':')) != NULL) {
		/* if connector name doesn't match, we're done */
		if (strncmp(connector_name, fwname, cp - fwname))
			return 0;
		fwname = cp + 1;
		if (*fwname == '\0')
			return 0;
	}

	fwedid = (struct edid *)do_edid_fw_load(connector, fwname, connector_name);
	if (fwedid == NULL)
		return 0;

	drm_mode_connector_update_edid_property(connector, fwedid);
	ret = drm_add_edid_modes(connector, fwedid);
	kfree(fwedid);

	return ret;
}

static u8 *
do_edid_fw_load(struct drm_connector *connector, const char *fwname,
			const char *connector_name)
{
	const struct firmware *fw = NULL;
	const u8 *fwdata;
	u8 *block = NULL, *new = NULL;
	int fwsize, expected;
	int j, valid_extensions = 0;
	bool print_bad_edid = !connector->bad_edid_counter || (drm_debug & DRM_UT_KMS);

	fw = firmware_get(fwname);

	if (fw == NULL) {
		DRM_ERROR("Requesting EDID firmware %s failed\n", fwname);
		return (NULL);
	}

	fwdata = fw->data;
	fwsize = fw->datasize;

	if (fwsize < EDID_LENGTH)
		goto fw_out;

	expected = (fwdata[0x7e] + 1) * EDID_LENGTH;
	if (expected != fwsize) {
		DRM_ERROR("Size of EDID firmware %s is invalid: %d vs %d(got)\n",
			  fwname, expected, fwsize);
		goto fw_out;
	}

	block = kmalloc(fwsize, M_DRM, GFP_KERNEL);
	if (block == NULL) {
		goto fw_out;
	}
	memcpy(block, fwdata, fwsize);

	/* now it is safe to release the firmware */
fw_out:
	fwdata = NULL;
	if (fw != NULL) {
		/*
		 * Don't release edid fw right away, useful if / is
		 * still not mounted and/or we performing early kms
		 */
		firmware_put(fw, 0);
	}

	if (block == NULL)
		return (NULL);

	/* first check the base block */
	if (!drm_edid_block_valid(block, 0, print_bad_edid, NULL)) {
		connector->bad_edid_counter++;
		DRM_ERROR("EDID firmware %s base block is invalid ", fwname);
		goto out;
	}

	DRM_INFO("Got EDID base block from %s for connector %s\n", fwname, connector_name);

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		return block;

	/* XXX then extension blocks */
	WARN(1, "Loading EDID firmware with extensions is untested!\n");

	for (j = 1; j <= block[0x7e]; j++) {
		/* if we skiped any extension block we have to shuffle good ones */
		if (j != valid_extensions + 1) {
			memcpy(block + (valid_extensions + 1) * EDID_LENGTH,
			       block + (j * EDID_LENGTH), EDID_LENGTH);
		}
		if (drm_edid_block_valid(block + j * EDID_LENGTH, j, print_bad_edid, NULL)) {
			valid_extensions++;
		}
	}

	if (valid_extensions != block[0x7e]) {
		block[EDID_LENGTH-1] += block[0x7e] - valid_extensions;
		block[0x7e] = valid_extensions;
		new = krealloc(block, (valid_extensions + 1) * EDID_LENGTH, M_DRM, M_WAITOK);
		if (new == NULL)
			goto out;
		block = new;
	}

	if (valid_extensions > 0) {
		DRM_INFO("Got %d extensions in EDID firmware from %s for connector %s\n",
			 valid_extensions, fwname, connector_name);
	}

	/* if got to here return edid block */
	return block;

out:
	kfree(block);
	return (NULL);
}

#endif /* CONFIG_DRM_LOAD_EDID_FIRMWARE */
