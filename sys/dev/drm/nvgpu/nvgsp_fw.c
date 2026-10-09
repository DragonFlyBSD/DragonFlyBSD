/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Loader glue for NVIDIA-distributed GSP-boot firmware blobs.
 *
 * Firmware blobs (booter, GSP image, etc.) are installed as plain
 * linux-firmware files.  firmware_get() resolves those names through
 * firmware(9)'s configured search path and we hold its references for the
 * lifetime of the device.
 *
 * Naming convention: linux-firmware style paths such as
 *   "nvidia/<firmware-chip>/gsp/booter_load-570.144.bin"
 *
 * The chip module owns this naming policy; GSP boot does not select firmware
 * versions at runtime.
 */

#include "nvgsp_priv.h"

#include <sys/firmware.h>

int
nvgsp_fw_init(struct nvgsp_state *sc)
{
	const struct firmware *fw;
	char name[128];
	int error;

	error = nvgpu_chip_format_firmware_name(sc->chip,
	    NVGPU_FIRMWARE_BOOTER_LOAD, name, sizeof(name));
	if (error != 0)
		return (error);

	fw = firmware_get(name);
	if (fw == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fw: cannot load \"%s\"\n", name);
		return (ENOENT);
	}

	sc->fw_booter_load = fw;
	nvgpu_log(NVGPU_LOG_DEBUG, "fw: %s loaded, %zu bytes, version %u, first 8: "
	    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
	    name,
	    fw->datasize, fw->version,
	    fw->data[0], fw->data[1], fw->data[2], fw->data[3],
	    fw->data[4], fw->data[5], fw->data[6], fw->data[7]);

	/* Shutdown blob: without it kldunload cannot tear down WPR2, so a
	 * later attach would fail; attach itself works fine, so this is a
	 * warning, not an error. */
	error = nvgpu_chip_format_firmware_name(sc->chip,
	    NVGPU_FIRMWARE_BOOTER_UNLOAD, name, sizeof(name));
	if (error != 0) {
		firmware_put(sc->fw_booter_load, FIRMWARE_UNLOAD);
		sc->fw_booter_load = NULL;
		return (error);
	}

	sc->fw_booter_unload = firmware_get(name);
	if (sc->fw_booter_unload == NULL)
		nvgpu_log(NVGPU_LOG_INFO, "fw: \"%s\" missing; kldunload will leave WPR2 set\n",
		    name);

	return (0);
}

void
nvgsp_fw_fini(struct nvgsp_state *sc)
{
	if (sc->fw_booter_unload != NULL) {
		firmware_put(sc->fw_booter_unload, FIRMWARE_UNLOAD);
		sc->fw_booter_unload = NULL;
	}
	if (sc->fw_booter_load != NULL) {
		firmware_put(sc->fw_booter_load, FIRMWARE_UNLOAD);
		sc->fw_booter_load = NULL;
	}
}
