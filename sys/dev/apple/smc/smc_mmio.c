/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Ported from FreeBSD's asmc(4) driver.
 *
 * DragonFlyBSD port: adapted for kmalloc/kfree, lockmgr 2-arg form,
 * taskqueue_start_threads ncpu arg, sys/bus_resource.h, acpica paths.
 * Added MMIO (T2) backend support.
 */

/*
 * Apple SMC — MMIO/T2 backend.
 *
 * apple_smc_try_enable_mmio() and the MMIO key read/write/getinfo/getbyindex
 * functions used on T2 (and later) Macs that expose the SMC over a memory-
 * mapped register window rather than ISA ports.
 */

#include "smc.h"

static int
apple_smc_mmio_wait(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int i, delay_us = 10;
	uint8_t status;

	for (i = 0; i < ASMC_MMIO_MAX_WAIT; i++) {
		status = bus_read_1(sc->sc_iomem, ASMC_MMIO_STATUS);
		if (status & ASMC_MMIO_STATUS_READY)
			return (0);
		DELAY(delay_us);
		if (delay_us < 3200)
			delay_us *= 2;
	}
	return (ETIMEDOUT);
}

int
apple_smc_mmio_key_read(device_t dev, const char *key,
    uint8_t *buf, uint8_t len)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint32_t key_int;
	int error, i;
	uint8_t cmd_result, rlen;

	if (len > ASMC_MAXVAL)
		return (EINVAL);

	SMC_LOCK(sc);

	if (bus_read_1(sc->sc_iomem, ASMC_MMIO_STATUS))
		bus_write_1(sc->sc_iomem, ASMC_MMIO_STATUS, 0);

	memcpy(&key_int, key, 4);
	bus_write_4(sc->sc_iomem, ASMC_MMIO_KEY_NAME, key_int);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_SMC_ID, 0);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_CMD, ASMC_CMDREAD);

	error = apple_smc_mmio_wait(dev);
	if (error != 0) {
		SMC_UNLOCK(sc);
		return (error);
	}

	cmd_result = bus_read_1(sc->sc_iomem, ASMC_MMIO_CMD);
	if (cmd_result != 0) {
		SMC_UNLOCK(sc);
		return (EIO);
	}

	rlen = bus_read_1(sc->sc_iomem, ASMC_MMIO_DATA_LEN);
	if (rlen > len) rlen = len;
	for (i = 0; i < rlen; i++)
		buf[i] = bus_read_1(sc->sc_iomem, ASMC_MMIO_DATA + i);

	SMC_UNLOCK(sc);
	return (0);
}

int
apple_smc_mmio_key_write(device_t dev, const char *key,
    uint8_t *buf, uint8_t len)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint32_t key_int;
	int error, i;
	uint8_t cmd_result;

	if (len > ASMC_MAXVAL)
		return (EINVAL);

	SMC_LOCK(sc);

	bus_write_1(sc->sc_iomem, ASMC_MMIO_STATUS, 0);
	for (i = 0; i < len; i++)
		bus_write_1(sc->sc_iomem, ASMC_MMIO_DATA + i, buf[i]);

	memcpy(&key_int, key, 4);
	bus_write_4(sc->sc_iomem, ASMC_MMIO_KEY_NAME, key_int);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_DATA_LEN, len);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_SMC_ID, 0);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_CMD, ASMC_CMDWRITE);

	error = apple_smc_mmio_wait(dev);
	if (error != 0) {
		SMC_UNLOCK(sc);
		return (error);
	}
	cmd_result = bus_read_1(sc->sc_iomem, ASMC_MMIO_CMD);
	SMC_UNLOCK(sc);
	return (cmd_result == 0 ? 0 : EIO);
}

int
apple_smc_mmio_key_getinfo(device_t dev, const char *key,
    uint8_t *len, char *type)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint32_t key_int;
	int error, i;
	uint8_t cmd_result;

	SMC_LOCK(sc);

	bus_write_1(sc->sc_iomem, ASMC_MMIO_STATUS, 0);
	memcpy(&key_int, key, 4);
	bus_write_4(sc->sc_iomem, ASMC_MMIO_KEY_NAME, key_int);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_SMC_ID, 0);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_CMD, ASMC_CMDGETINFO);

	error = apple_smc_mmio_wait(dev);
	if (error != 0) {
		SMC_UNLOCK(sc);
		return (error);
	}
	cmd_result = bus_read_1(sc->sc_iomem, ASMC_MMIO_CMD);
	if (cmd_result != 0) {
		SMC_UNLOCK(sc);
		return (EIO);
	}
	if (type != NULL) {
		for (i = 0; i < ASMC_TYPELEN; i++)
			type[i] = bus_read_1(sc->sc_iomem, ASMC_MMIO_DATA + i);
		type[ASMC_TYPELEN] = '\0';
	}
	if (len != NULL)
		*len = bus_read_1(sc->sc_iomem, ASMC_MMIO_DATA + 5);

	SMC_UNLOCK(sc);
	return (0);
}

int
apple_smc_mmio_key_getbyindex(device_t dev, int index, char *key)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint32_t idx_val;
	int error, i;
	uint8_t cmd_result;

	SMC_LOCK(sc);

	bus_write_1(sc->sc_iomem, ASMC_MMIO_STATUS, 0);
	idx_val = htobe32(index);
	bus_write_4(sc->sc_iomem, ASMC_MMIO_KEY_NAME, idx_val);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_SMC_ID, 0);
	bus_write_1(sc->sc_iomem, ASMC_MMIO_CMD, ASMC_CMDGETBYINDEX);

	error = apple_smc_mmio_wait(dev);
	if (error != 0) {
		SMC_UNLOCK(sc);
		return (error);
	}
	cmd_result = bus_read_1(sc->sc_iomem, ASMC_MMIO_CMD);
	if (cmd_result != 0) {
		SMC_UNLOCK(sc);
		return (EIO);
	}
	for (i = 0; i < ASMC_KEYLEN; i++)
		key[i] = bus_read_1(sc->sc_iomem, ASMC_MMIO_DATA + i);
	key[ASMC_KEYLEN] = '\0';

	SMC_UNLOCK(sc);
	return (0);
}

int
apple_smc_try_enable_mmio(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	u_long size;
	uint8_t status, ldkn, rgen;
	int error;

	size = rman_get_size(sc->sc_iomem);
	if (size < ASMC_MMIO_MIN_SIZE)
		return (ENXIO);

	status = bus_read_1(sc->sc_iomem, ASMC_MMIO_STATUS);
	if (status == 0xFF)
		return (ENXIO);

	error = apple_smc_mmio_key_read(dev, ASMC_KEY_LDKN, &ldkn, 1);
	if (error != 0)
		return (ENXIO);

	/* RGEN=3 indicates T2 coprocessor; LDKN just tracks firmware key gen */
	rgen = 0;
	error = apple_smc_mmio_key_read(dev, ASMC_KEY_RGEN, &rgen, 1);
	if (error != 0) {
		device_printf(dev, "MMIO: failed to read RGEN: %d\n", error);
	}
	sc->sc_is_t2 = (rgen == 3);

	device_printf(dev, "MMIO: LDKN=%d%s\n", ldkn,
	    sc->sc_is_t2 ? ", T2 detected" : "");
	return (0);
}
