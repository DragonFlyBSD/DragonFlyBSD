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
 * Apple SMC — ISA port I/O backend.
 *
 * Low-level read/write/wait functions that talk to port 0x300, plus fan,
 * temperature, Sudden Motion Sensor helpers, and sensor-discovery utilities
 * shared by attach and the sysctl layer.
 */

#include "smc.h"

static int
apple_smc_wait_ack(device_t dev, uint8_t val, int amount)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	u_int i;

	val = val & ASMC_STATUS_MASK;
	for (i = 0; i < amount; i++) {
		if ((ASMC_CMDPORT_READ(sc) & ASMC_STATUS_MASK) == val)
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

int
apple_smc_wait(device_t dev, uint8_t val)
{
	if (apple_smc_wait_ack(dev, val, 1000) == 0)
		return (0);
#ifdef APPLE_SMC_DEBUG
	{
		struct apple_smc_softc *sc = device_get_softc(dev);
		device_printf(dev, "%s failed: 0x%x, 0x%x\n", __func__,
		    val & ASMC_STATUS_MASK, ASMC_CMDPORT_READ(sc));
	}
#endif
	return (ETIMEDOUT);
}

int
apple_smc_command(device_t dev, uint8_t command)
{
	int i;
	struct apple_smc_softc *sc = device_get_softc(dev);

	for (i = 0; i < 10; i++) {
		ASMC_CMDPORT_WRITE(sc, command);
		if (apple_smc_wait_ack(dev, 0x0c, 100) == 0)
			return (0);
	}
	return (EIO);
}

int
apple_smc_key_read(device_t dev, const char *key, uint8_t *buf, uint8_t len)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int i, error = EIO, try = 0;

	if (sc->sc_is_mmio)
		return (apple_smc_mmio_key_read(dev, key, buf, len));

	SMC_LOCK(sc);

begin:
	if (apple_smc_command(dev, ASMC_CMDREAD))
		goto out;
	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (apple_smc_wait(dev, 0x04))
			goto out;
	}
	ASMC_DATAPORT_WRITE(sc, len);
	for (i = 0; i < len; i++) {
		if (apple_smc_wait(dev, 0x05))
			goto out;
		buf[i] = ASMC_DATAPORT_READ(sc);
	}
	error = 0;
out:
	if (error) {
		if (++try < 10)
			goto begin;
		device_printf(dev, "%s for key %s failed %d times\n",
		    __func__, key, try);
	}
	SMC_UNLOCK(sc);
	return (error);
}

int
apple_smc_key_write(device_t dev, const char *key, uint8_t *buf, uint8_t len)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int i, error = EIO, try = 0;

	if (sc->sc_is_mmio)
		return (apple_smc_mmio_key_write(dev, key, buf, len));

	SMC_LOCK(sc);

begin:
	if (apple_smc_command(dev, ASMC_CMDWRITE))
		goto out;
	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (apple_smc_wait(dev, 0x04))
			goto out;
	}
	ASMC_DATAPORT_WRITE(sc, len);
	for (i = 0; i < len; i++) {
		if (apple_smc_wait(dev, 0x04))
			goto out;
		ASMC_DATAPORT_WRITE(sc, buf[i]);
	}
	error = 0;
out:
	if (error) {
		if (++try < 10)
			goto begin;
		device_printf(dev, "%s for key %s failed %d times\n",
		    __func__, key, try);
	}
	SMC_UNLOCK(sc);
	return (error);
}

int
apple_smc_key_getinfo(device_t dev, const char *key, uint8_t *len, char *type)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t info[ASMC_KEYINFO_RESPLEN];
	int i, error = EIO, try = 0;

	if (sc->sc_is_mmio)
		return (apple_smc_mmio_key_getinfo(dev, key, len, type));

	SMC_LOCK(sc);

begin:
	if (apple_smc_command(dev, ASMC_CMDGETINFO))
		goto out;
	for (i = 0; i < ASMC_KEYLEN; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (apple_smc_wait(dev, ASMC_STATUS_AWAIT_DATA))
			goto out;
	}
	ASMC_DATAPORT_WRITE(sc, ASMC_KEYINFO_RESPLEN);
	for (i = 0; i < ASMC_KEYINFO_RESPLEN; i++) {
		if (apple_smc_wait(dev, ASMC_STATUS_DATA_READY))
			goto out;
		info[i] = ASMC_DATAPORT_READ(sc);
	}
	error = 0;
out:
	if (error && ++try < ASMC_MAXRETRIES)
		goto begin;
	SMC_UNLOCK(sc);

	if (error == 0) {
		if (len != NULL)
			*len = info[0];
		if (type != NULL) {
			for (i = 0; i < ASMC_TYPELEN; i++)
				type[i] = info[i + 1];
			type[ASMC_TYPELEN] = '\0';
		}
	}
	return (error);
}

int
apple_smc_key_dump_by_index(device_t dev, int index, char *key_out,
    char *type_out, uint8_t *len_out)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t index_buf[ASMC_KEYLEN];
	uint8_t key_buf[ASMC_KEYLEN];
	uint8_t info_buf[ASMC_KEYINFO_RESPLEN];
	int error = ENXIO, try = 0;
	int i;

	if (sc->sc_is_mmio) {
		error = apple_smc_mmio_key_getbyindex(dev, index, key_out);
		if (error != 0)
			return (error);
		return (apple_smc_mmio_key_getinfo(dev, key_out,
		    len_out, type_out));
	}

	SMC_LOCK(sc);

	index_buf[0] = (index >> 24) & 0xff;
	index_buf[1] = (index >> 16) & 0xff;
	index_buf[2] = (index >> 8) & 0xff;
	index_buf[3] = index & 0xff;

begin:
	if (apple_smc_command(dev, ASMC_CMDGETBYINDEX))
		goto out;
	for (i = 0; i < ASMC_KEYLEN; i++) {
		ASMC_DATAPORT_WRITE(sc, index_buf[i]);
		if (apple_smc_wait(dev, ASMC_STATUS_AWAIT_DATA))
			goto out;
	}
	ASMC_DATAPORT_WRITE(sc, ASMC_KEYLEN);
	for (i = 0; i < ASMC_KEYLEN; i++) {
		if (apple_smc_wait(dev, ASMC_STATUS_DATA_READY))
			goto out;
		key_buf[i] = ASMC_DATAPORT_READ(sc);
	}
	if (apple_smc_command(dev, ASMC_CMDGETINFO))
		goto out;
	for (i = 0; i < ASMC_KEYLEN; i++) {
		ASMC_DATAPORT_WRITE(sc, key_buf[i]);
		if (apple_smc_wait(dev, ASMC_STATUS_AWAIT_DATA))
			goto out;
	}
	ASMC_DATAPORT_WRITE(sc, ASMC_KEYINFO_RESPLEN);
	for (i = 0; i < ASMC_KEYINFO_RESPLEN; i++) {
		if (apple_smc_wait(dev, ASMC_STATUS_DATA_READY))
			goto out;
		info_buf[i] = ASMC_DATAPORT_READ(sc);
	}
	memcpy(key_out, key_buf, ASMC_KEYLEN);
	key_out[ASMC_KEYLEN] = '\0';
	*len_out = info_buf[0];
	memcpy(type_out, &info_buf[1], ASMC_TYPELEN);
	type_out[ASMC_TYPELEN] = '\0';
	error = 0;
out:
	if (error && ++try < ASMC_MAXRETRIES)
		goto begin;
	SMC_UNLOCK(sc);
	return (error);
}

int
apple_smc_key_search(device_t dev, const char *prefix, unsigned int *idx)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	unsigned int lo, hi, mid;
	char key[ASMC_KEYLEN + 1];
	char type[ASMC_TYPELEN + 1];
	uint8_t len;
	int error;

	lo = 0;
	hi = sc->sc_nkeys;
	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		error = apple_smc_key_dump_by_index(dev, mid, key, type, &len);
		if (error != 0)
			return (error);
		if (strncmp(key, prefix, ASMC_KEYLEN) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	*idx = lo;
	return (0);
}

int
apple_smc_sensor_type_supported(const char *type)
{
	return (strncmp(type, "sp78", 4) == 0 ||
	    strncmp(type, "sp87", 4) == 0 ||
	    strncmp(type, "sp4b", 4) == 0 ||
	    strncmp(type, "sp5a", 4) == 0 ||
	    strncmp(type, "sp69", 4) == 0 ||
	    strncmp(type, "sp96", 4) == 0 ||
	    strncmp(type, "sp2d", 4) == 0 ||
	    strncmp(type, "ui16", 4) == 0);
}

int
apple_smc_sensor_read(device_t dev, const char *key, int *millivalue)
{
	uint8_t buf[2];
	char type[ASMC_TYPELEN + 1];
	uint8_t len;
	int error;
	int16_t sv;

	error = apple_smc_key_getinfo(dev, key, &len, type);
	if (error != 0 || len != 2)
		return (error ? error : ENXIO);

	error = apple_smc_key_read(dev, key, buf, sizeof(buf));
	if (error != 0)
		return (error);

	sv = (int16_t)be16dec(buf);

	if (strncmp(type, "sp78", 4) == 0)
		*millivalue = ((int)sv * 1000) / 256;
	else if (strncmp(type, "sp87", 4) == 0)
		*millivalue = ((int)sv * 1000) / 128;
	else if (strncmp(type, "sp4b", 4) == 0)
		*millivalue = ((int)sv * 1000) / 2048;
	else if (strncmp(type, "sp5a", 4) == 0)
		*millivalue = ((int)sv * 1000) / 1024;
	else if (strncmp(type, "sp69", 4) == 0)
		*millivalue = ((int)sv * 1000) / 512;
	else if (strncmp(type, "sp96", 4) == 0)
		*millivalue = ((int)sv * 1000) / 64;
	else if (strncmp(type, "sp2d", 4) == 0)
		*millivalue = ((int)sv * 1000) / 8192;
	else if (strncmp(type, "ui16", 4) == 0)
		*millivalue = be16dec(buf);
	else
		return (ENXIO);

	return (0);
}

void
apple_smc_scan_sensor_range(device_t dev, unsigned int start,
    unsigned int end, char prefix, int *countp, char **sensors, int maxcount)
{
	char key[ASMC_KEYLEN + 1];
	char type[ASMC_TYPELEN + 1];
	uint8_t len;
	unsigned int i;
	char *sensor_key;

	for (i = start; i < end; i++) {
		if (apple_smc_key_dump_by_index(dev, i, key, type, &len))
			continue;
		if (key[0] != prefix || len != 2)
			continue;
		if (!apple_smc_sensor_type_supported(type))
			continue;
		if (*countp >= maxcount)
			break;
		sensor_key = kmalloc(ASMC_KEYLEN + 1, M_DEVBUF, M_WAITOK);
		memcpy(sensor_key, key, ASMC_KEYLEN + 1);
		sensors[(*countp)++] = sensor_key;
	}
}

/* Fan */

int
apple_smc_fan_count(device_t dev)
{
	uint8_t buf[1];

	if (apple_smc_key_read(dev, ASMC_KEY_FANCOUNT, buf, sizeof(buf)) != 0)
		return (-1);
	return (buf[0]);
}

static uint32_t
apple_smc_float_to_u32(uint32_t d)
{
	int32_t exp;
	uint32_t fr;

	/*
	 * Sign bit set means negative RPM, which is physically impossible.
	 * Clamp to 0 rather than returning a garbage large value.
	 */
	if (d == 0 || (d >> 31) != 0)
		return (0);
	exp = (int32_t)((d >> 23) & 0xff) - 0x7f;
	fr = d & 0x7fffff;
	if (exp < 0) return (0);
	if (exp > 23) {
		if (exp > 30) return (0xffffffffu);
		return ((1u << exp) | (fr << (exp - 23)));
	}
	return ((1u << exp) + (fr >> (23 - exp)));
}

static uint32_t
apple_smc_u32_to_float(uint32_t d)
{
	uint32_t dc, bc, exp;

	if (d == 0) return (0);
	dc = d; bc = 0;
	while (dc >>= 1) ++bc;
	if (bc > 30) bc = 30;
	exp = 0x7f + bc;
	if (bc >= 23)
		return ((exp << 23) | ((d >> (bc - 23)) & 0x7fffff));
	else
		return ((exp << 23) | ((d << (23 - bc)) & 0x7fffff));
}

int
apple_smc_fan_getvalue(device_t dev, const char *key, int fan)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t buf[4];
	char fankey[5];
	char type[ASMC_TYPELEN + 1];

	ksnprintf(fankey, sizeof(fankey), key, fan);

	if (sc->sc_is_t2 &&
	    apple_smc_key_getinfo(dev, fankey, NULL, type) == 0 &&
	    strncmp(type, "flt ", 4) == 0) {
		if (apple_smc_key_read(dev, fankey, buf, 4) != 0)
			return (-1);
		return ((int)apple_smc_float_to_u32(le32dec(buf)));
	}
	if (apple_smc_key_read(dev, fankey, buf, 2) != 0)
		return (-1);
	return ((buf[0] << 6) | (buf[1] >> 2));
}

/*
 * ASMC_FAN_NAME_OFFSET: the FxID key payload is an 8-byte struct where the
 * first 4 bytes are the key name echoed back (e.g. "F0ID") and bytes 4-7
 * hold the NUL-terminated fan model string.  Callers pass buflen >= 8.
 */
#define ASMC_FAN_NAME_OFFSET	4

char *
apple_smc_fan_getstring(device_t dev, const char *key, int fan,
    uint8_t *buf, uint8_t buflen)
{
	char fankey[5];

	ksnprintf(fankey, sizeof(fankey), key, fan);
	if (apple_smc_key_read(dev, fankey, buf, buflen) != 0)
		return (NULL);
	/* Skip the 4-byte key-name prefix embedded in the FxID payload. */
	return ((char *)buf + ASMC_FAN_NAME_OFFSET);
}

int
apple_smc_fan_setvalue(device_t dev, const char *key, int fan, int speed)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t buf[4];
	char fankey[5];
	char type[ASMC_TYPELEN + 1];

	ksnprintf(fankey, sizeof(fankey), key, fan);

	if (sc->sc_is_t2 &&
	    apple_smc_key_getinfo(dev, fankey, NULL, type) == 0 &&
	    strncmp(type, "flt ", 4) == 0) {
		uint32_t fval;
		if (speed < 0) speed = 0;
		if (speed > 65535) speed = 65535;
		fval = apple_smc_u32_to_float((uint32_t)speed);
		le32enc(buf, fval);
		return (apple_smc_key_write(dev, fankey, buf, 4));
	}
	speed *= 4;
	buf[0] = speed >> 8;
	buf[1] = speed;
	return (apple_smc_key_write(dev, fankey, buf, 2));
}

/*
 * Read a temperature key and return millidegrees Celsius.  Delegate to the
 * generic sensor decoder so temperature sysctls and generic sensor sysctls
 * stay bit-for-bit consistent for sp78 and any future supported format.
 *
 * Returns -1 on read/decode failure (legacy callers already treat negative as
 * error).
 */
int
apple_smc_temp_getvalue(device_t dev, const char *key)
{
	int millivalue;
	int error;

	error = apple_smc_sensor_read(dev, key, &millivalue);
	if (error != 0)
		return (-1);
	return (millivalue);
}

/* Sudden Motion Sensor */

int
apple_smc_sms_read(device_t dev, const char *key, int16_t *val)
{
	uint8_t buf[2];
	int error;

	switch (key[3]) {
	case 'X': case 'Y': case 'Z':
		error = apple_smc_key_read(dev, key, buf, sizeof(buf));
		break;
	default:
		device_printf(dev, "%s called with invalid key %s\n",
		    __func__, key);
		return (EINVAL);
	}
	if (error == 0)
		*val = ((int16_t)buf[0] << 8) | buf[1];
	return (error);
}

void
apple_smc_sms_calibrate(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);

	apple_smc_sms_read(dev, ASMC_KEY_SMS_X, &sc->sms_rest_x);
	apple_smc_sms_read(dev, ASMC_KEY_SMS_Y, &sc->sms_rest_y);
	apple_smc_sms_read(dev, ASMC_KEY_SMS_Z, &sc->sms_rest_z);
}

void
apple_smc_sms_init(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t buf[2];
	int i;
	int retry_ticks;
	int error;

	buf[0] = 0x01;
	error = apple_smc_key_write(dev, ASMC_KEY_INTOK, buf, 1);
	if (error != 0) {
		device_printf(dev, "sms init: INTOK write failed: %d\n", error);
	}
	DELAY(50);

	buf[0] = 20;
	error = apple_smc_key_write(dev, ASMC_KEY_SMS_LOW_INT, buf, 1);
	if (error != 0) {
		device_printf(dev, "sms init: MOLD write failed: %d\n", error);
	}
	DELAY(200);

	buf[0] = 20;
	error = apple_smc_key_write(dev, ASMC_KEY_SMS_HIGH_INT, buf, 1);
	if (error != 0) {
		device_printf(dev, "sms init: MOHD write failed: %d\n", error);
	}
	DELAY(200);

	buf[0] = 0x00;
	buf[1] = 0x60;
	error = apple_smc_key_write(dev, ASMC_KEY_SMS_LOW, buf, 2);
	if (error != 0) {
		device_printf(dev, "sms init: MOLT write failed: %d\n", error);
	}
	DELAY(200);

	buf[0] = 0x01;
	buf[1] = 0xc0;
	error = apple_smc_key_write(dev, ASMC_KEY_SMS_HIGH, buf, 2);
	if (error != 0) {
		device_printf(dev, "sms init: MOHT write failed: %d\n", error);
	}
	DELAY(200);

	buf[0] = 0x01;
	error = apple_smc_key_write(dev, ASMC_KEY_SMS_FLAG, buf, 1);
	if (error != 0) {
		device_printf(dev, "sms init: MSDW write failed: %d\n", error);
	}
	DELAY(100);

	retry_ticks = hz / 100;
	if (retry_ticks < 1) {
		retry_ticks = 1;
	}
	sc->sc_sms_intr_works = 0;
	for (i = 0; i < 5; i++) {
		if (apple_smc_key_read(dev, ASMC_KEY_SMS, buf, 2) == 0 &&
		    buf[0] == ASMC_SMS_INIT1 && buf[1] == ASMC_SMS_INIT2) {
			sc->sc_sms_intr_works = 1;
			goto done;
		}
		buf[0] = ASMC_SMS_INIT1;
		buf[1] = ASMC_SMS_INIT2;
		error = apple_smc_key_write(dev, ASMC_KEY_SMS, buf, 2);
		if (error != 0) {
			device_printf(dev, "sms init: MOCN write failed: %d\n",
			    error);
		}
		/*
		 * The old code busy-spun for 50 ms in attach.  Sleep instead:
		 * 5 retries at ~10 ms each on hz=100 preserves the same total
		 * wait budget without burning the CPU in early boot/attach.
		 */
		tsleep(sc, 0, "smsini", retry_ticks);
	}
	device_printf(dev, "WARNING: Sudden Motion Sensor not initialized!\n");
done:
	apple_smc_sms_calibrate(dev);
}

void
apple_smc_sms_intr(void *arg)
{
	device_t dev = (device_t)arg;
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t type;

	if (!sc->sc_sms_intr_works)
		return;

	SMC_LOCK(sc);
	type = ASMC_INTPORT_READ(sc);
	sc->sc_sms_intrtype = type;
	SMC_UNLOCK(sc);

	apple_smc_sms_printintr(dev, type);

	if (type == ASMC_ALSL_INT2A && sc->sc_has_alsl)
		return;

	taskqueue_enqueue(sc->sc_sms_tq, &sc->sc_sms_task);
}

void
apple_smc_sms_printintr(device_t dev, uint8_t type)
{
	struct apple_smc_softc *sc = device_get_softc(dev);

	switch (type) {
	case ASMC_SMS_INTFF:
		device_printf(dev, "WARNING: possible free fall!\n");
		break;
	case ASMC_SMS_INTHA:
		device_printf(dev, "WARNING: high acceleration detected!\n");
		break;
	case ASMC_SMS_INTSH:
		device_printf(dev, "WARNING: possible shock!\n");
		break;
	case ASMC_ALSL_INT2A:
		if (sc->sc_has_alsl)
			break;
		/* FALLTHROUGH */
	default:
		device_printf(dev, "unknown interrupt: 0x%x\n", type);
	}
}

void
apple_smc_sms_task(void *arg, int pending)
{
	struct apple_smc_softc *sc = (struct apple_smc_softc *)arg;
	char notify[16];
	int type;
	uint8_t intrtype;

	SMC_LOCK(sc);
	intrtype = sc->sc_sms_intrtype;
	SMC_UNLOCK(sc);

	switch (intrtype) {
	case ASMC_SMS_INTFF: type = 2; break;
	case ASMC_SMS_INTHA: type = 1; break;
	case ASMC_SMS_INTSH: type = 0; break;
	default:             type = 255; break;
	}
	ksnprintf(notify, sizeof(notify), " notify=0x%x", type);
	devctl_notify("ACPI", "apple_smc", "SMS", notify);
}
