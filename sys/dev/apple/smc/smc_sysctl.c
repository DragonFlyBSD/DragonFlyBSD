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
 * Apple SMC — sysctl handlers.
 *
 * All SYSCTL_HANDLER_ARGS callbacks: fan speed, temperature, SMS axes,
 * keyboard backlight, battery charge limit, system state keys, and
 * the optional raw-key debug interface.
 */

#include "smc.h"

/*
 * Module-scope cached keyboard backlight level.  Shared across all device
 * instances so suspend/resume can restore the last requested brightness even
 * though the resume path only has a device_t and not sysctl request context.
 */
static unsigned int light_control = 0;

/* Retrieve the cached module-level brightness value for resume restore. */
unsigned int
apple_smc_get_light_control(void)
{
	return (light_control);
}

int
apple_smc_mb_sysctl_fanspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	int fan = arg2;
	int32_t v;

	v = apple_smc_fan_getvalue(dev, ASMC_KEY_FANSPEED, fan);
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_mb_sysctl_fanid(SYSCTL_HANDLER_ARGS)
{
	uint8_t buf[16];
	device_t dev = (device_t)arg1;
	int fan = arg2;
	char *desc;

	desc = apple_smc_fan_getstring(dev, ASMC_KEY_FANID, fan,
	    buf, sizeof(buf));
	if (desc == NULL) {
		return (EIO);
	}
	return (sysctl_handle_string(oidp, desc, 0, req));
}

int
apple_smc_mb_sysctl_fansafespeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	int fan = arg2;
	int32_t v;

	v = apple_smc_fan_getvalue(dev, ASMC_KEY_FANSAFESPEED, fan);
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_mb_sysctl_fanminspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = apple_smc_fan_getvalue(dev, ASMC_KEY_FANMINSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		error = apple_smc_fan_setvalue(dev, ASMC_KEY_FANMINSPEED, fan, v);
	}
	return (error);
}

int
apple_smc_mb_sysctl_fanmaxspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = apple_smc_fan_getvalue(dev, ASMC_KEY_FANMAXSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		error = apple_smc_fan_setvalue(dev, ASMC_KEY_FANMAXSPEED, fan, v);
	}
	return (error);
}

int
apple_smc_mb_sysctl_fantargetspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = apple_smc_fan_getvalue(dev, ASMC_KEY_FANTARGETSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		error = apple_smc_fan_setvalue(dev, ASMC_KEY_FANTARGETSPEED, fan, v);
	}
	return (error);
}

int
apple_smc_mb_sysctl_fanmanual(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int fan = arg2;
	int error;
	int32_t v;
	uint8_t buf[2];
	uint16_t val;
	char fmkey[5];

	ksnprintf(fmkey, sizeof(fmkey), ASMC_KEY_FANMANUAL_T2, fan);
	if (sc->sc_is_t2 &&
	    apple_smc_key_getinfo(dev, fmkey, NULL, NULL) == 0) {
		error = apple_smc_key_read(dev, fmkey, buf, 1);
		if (error != 0) {
			return (error);
		}
		v = buf[0] ? 1 : 0;
		error = sysctl_handle_int(oidp, &v, 0, req);
		if (error == 0 && req->newptr != NULL) {
			if (v != 0 && v != 1) {
				return (EINVAL);
			}
			buf[0] = (uint8_t)v;
			error = apple_smc_key_write(dev, fmkey, buf, 1);
		}
		return (error);
	}

	error = apple_smc_key_read(dev, ASMC_KEY_FANMANUAL, buf, sizeof(buf));
	if (error != 0) {
		return (error);
	}

	val = (buf[0] << 8) | buf[1];
	v = (val >> fan) & 0x01;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (v != 0 && v != 1) {
			return (EINVAL);
		}
		error = apple_smc_key_read(dev, ASMC_KEY_FANMANUAL,
		    buf, sizeof(buf));
		if (error == 0) {
			val = (buf[0] << 8) | buf[1];
			if (v) {
				val |= (1 << fan);
			} else {
				val &= ~(1 << fan);
			}
			buf[0] = val >> 8;
			buf[1] = val & 0xff;
			error = apple_smc_key_write(dev, ASMC_KEY_FANMANUAL,
			    buf, sizeof(buf));
		}
	}
	return (error);
}

int
apple_smc_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int error;
	int val;

	if (arg2 < 0 || arg2 >= sc->sc_temp_count) {
		return (EINVAL);
	}
	error = apple_smc_sensor_read(dev, sc->sc_temp_sensors[arg2], &val);
	if (error != 0) {
		return (error);
	}
	return (sysctl_handle_int(oidp, &val, 0, req));
}

int
apple_smc_sensor_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int error, val;
	int sensor_type = (arg2 >> 8) & 0xFF;
	int sensor_idx  = arg2 & 0xFF;
	const char *key = NULL;

	switch (sensor_type) {
	case 'V':
		if (sensor_idx < sc->sc_voltage_count) {
			key = sc->sc_voltage_sensors[sensor_idx];
		}
		break;
	case 'I':
		if (sensor_idx < sc->sc_current_count) {
			key = sc->sc_current_sensors[sensor_idx];
		}
		break;
	case 'P':
		if (sensor_idx < sc->sc_power_count) {
			key = sc->sc_power_sensors[sensor_idx];
		}
		break;
	case 'L':
		if (sensor_idx < sc->sc_light_count) {
			key = sc->sc_light_sensors[sensor_idx];
		}
		break;
	default:
		return (EINVAL);
	}
	if (key == NULL) {
		return (ENOENT);
	}

	error = apple_smc_sensor_read(dev, key, &val);
	if (error != 0) {
		return (error);
	}
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
apple_smc_sms_axis(device_t dev, const char *key,
    struct sysctl_oid *oidp, struct sysctl_req *req)
{
	int16_t val;
	int32_t v;
	int error;

	error = apple_smc_sms_read(dev, key, &val);
	if (error != 0) {
		return (error);
	}
	v = (int32_t)val;
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_mb_sysctl_sms_x(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_sms_axis((device_t)arg1, ASMC_KEY_SMS_X, oidp, req));
}

int
apple_smc_mb_sysctl_sms_y(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_sms_axis((device_t)arg1, ASMC_KEY_SMS_Y, oidp, req));
}

int
apple_smc_mb_sysctl_sms_z(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_sms_axis((device_t)arg1, ASMC_KEY_SMS_Z, oidp, req));
}

static int
apple_smc_light_sensor(device_t dev, const char *key,
    struct sysctl_oid *oidp, struct sysctl_req *req)
{
	uint8_t buf[6];
	int error;
	int32_t v;

	error = apple_smc_key_read(dev, key, buf, sizeof(buf));
	if (error != 0) {
		return (error);
	}
	v = buf[2];
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_mbp_sysctl_light_left(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_light_sensor((device_t)arg1, ASMC_KEY_LIGHTLEFT,
	    oidp, req));
}

int
apple_smc_mbp_sysctl_light_right(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_light_sensor((device_t)arg1, ASMC_KEY_LIGHTRIGHT,
	    oidp, req));
}

int
apple_smc_mbp_sysctl_light_left_10byte(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t buf[10];
	uint32_t v;
	int error;

	error = apple_smc_key_read(dev, ASMC_KEY_LIGHTLEFT, buf, sizeof(buf));
	if (error != 0) {
		return (error);
	}
	v = be32dec(&buf[6]);
	v = v >> 8;
	if (v > 255) {
		v = 255;
	}
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_mbp_sysctl_light_control(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t buf[2];
	int error, v;

	v = light_control;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (v < 0 || v > 255) {
			return (EINVAL);
		}
		light_control = v;
		sc->sc_kbd_bkl_level = v * 100 / 255;
		buf[0] = light_control;
		buf[1] = 0x00;
		error = apple_smc_key_write(dev, ASMC_KEY_LIGHTVALUE, buf,
		    sizeof(buf));
	}
	return (error);
}

int
apple_smc_aupo_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t aupo;
	int val, error;

	if (apple_smc_key_read(dev, ASMC_KEY_AUPO, &aupo, 1) != 0) {
		return (EIO);
	}
	val = (aupo != 0) ? 1 : 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL) {
		return (error);
	}
	aupo = (val != 0) ? 1 : 0;
	return (apple_smc_key_write(dev, ASMC_KEY_AUPO, &aupo, 1) != 0 ?
	    EIO : 0);
}

int
apple_smc_bclm_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t bclm;
	int val, error;

	if (apple_smc_key_read(dev, ASMC_KEY_BCLM, &bclm, 1) != 0) {
		return (EIO);
	}
	val = (int)bclm;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL) {
		return (error);
	}
	if (val < 0 || val > 100) {
		return (EINVAL);
	}
	bclm = (uint8_t)val;
	return (apple_smc_key_write(dev, ASMC_KEY_BCLM, &bclm, 1) != 0 ?
	    EIO : 0);
}

static const char *
apple_smc_cause_str(int8_t cause)
{
	switch (cause) {
	case -128: return "power-loss";
	case -127: return "software-powerdown";
	case -125: return "thermtrip";
	case -50:  return "overtemp-shutdown";
	case -20:  return "watchdog";
	case -15:  return "battery-low";
	case 0:    return "unknown";
	case 1:    return "overtemp-sleep";
	case 3:    return "power-button";
	case 5:    return "good-shutdown";
	default:   return NULL;
	}
}

static int
apple_smc_cause_sysctl(device_t dev, const char *key,
    struct sysctl_oid *oidp, struct sysctl_req *req)
{
	int8_t cause;
	const char *desc;
	char buf[48];

	if (apple_smc_key_read(dev, key, (uint8_t *)&cause, 1) != 0) {
		return (EIO);
	}
	desc = apple_smc_cause_str(cause);
	if (desc) {
		ksnprintf(buf, sizeof(buf), "%d (%s)", (int)cause, desc);
	} else {
		ksnprintf(buf, sizeof(buf), "%d", (int)cause);
	}
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

int
apple_smc_mssd_sysctl(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_cause_sysctl((device_t)arg1, ASMC_KEY_MSSD,
	    oidp, req));
}

int
apple_smc_mssp_sysctl(SYSCTL_HANDLER_ARGS)
{
	return (apple_smc_cause_sysctl((device_t)arg1, ASMC_KEY_MSSP,
	    oidp, req));
}

int
apple_smc_msal_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t msal; char buf[80];

	if (apple_smc_key_read(dev, ASMC_KEY_MSAL, &msal, 1) != 0) {
		return (EIO);
	}
	/*
	 * Bits 0x04, 0x10 and 0x20 are present in observed hardware values but
	 * are not documented here yet, so report the raw byte and decode only the
	 * known flags.
	 */
	ksnprintf(buf, sizeof(buf),
	    "0x%02x (tss=%d therm_valid=%d calib_valid=%d prochot=%d plimits=%d)",
	    msal,
	    (msal & 0x01) != 0, (msal & 0x02) != 0,
	    (msal & 0x08) != 0, (msal & 0x40) != 0, (msal & 0x80) != 0);
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

int
apple_smc_clkt_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t buf[4]; uint32_t secs;

	if (apple_smc_key_read(dev, ASMC_KEY_CLKT, buf, 4) != 0) {
		return (EIO);
	}
	secs = be32dec(buf);
	return (sysctl_handle_32(oidp, &secs, 0, req));
}

int
apple_smc_msps_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t buf[2]; uint32_t state;

	if (apple_smc_key_read(dev, ASMC_KEY_MSPS, buf, 2) != 0) {
		return (EIO);
	}
	state = ((uint32_t)buf[0] << 8) | buf[1];
	return (sysctl_handle_32(oidp, &state, 0, req));
}

int
apple_smc_rplt_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t buf[9]; char name[9];

	memset(buf, 0, sizeof(buf));
	if (apple_smc_key_read(dev, ASMC_KEY_RPLT, buf, 8) != 0) {
		return (EIO);
	}
	memcpy(name, buf, 8);
	name[8] = '\0';
	return (sysctl_handle_string(oidp, name, sizeof(name), req));
}

int
apple_smc_rgen_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	uint8_t gen; uint32_t val;

	if (apple_smc_key_read(dev, ASMC_KEY_RGEN, &gen, 1) != 0) {
		return (EIO);
	}
	val = gen;
	return (sysctl_handle_32(oidp, &val, 0, req));
}

#ifdef APPLE_SMC_DEBUG
int
apple_smc_raw_key_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	char newkey[ASMC_KEYLEN + 1];
	uint8_t keylen;
	int error;

	strlcpy(newkey, sc->sc_rawkey, sizeof(newkey));
	error = sysctl_handle_string(oidp, newkey, sizeof(newkey), req);
	if (error || req->newptr == NULL) {
		return (error);
	}
	if (strlen(newkey) != ASMC_KEYLEN) {
		return (EINVAL);
	}
	if (apple_smc_key_getinfo(dev, newkey, &keylen, sc->sc_rawtype) != 0) {
		return (ENOENT);
	}
	if (keylen > ASMC_MAXVAL) {
		keylen = ASMC_MAXVAL;
	}
	strlcpy(sc->sc_rawkey, newkey, sizeof(sc->sc_rawkey));
	sc->sc_rawlen = keylen;
	memset(sc->sc_rawval, 0, sizeof(sc->sc_rawval));
	error = apple_smc_key_read(dev, sc->sc_rawkey, sc->sc_rawval,
	    sc->sc_rawlen);
	if (error != 0) {
		return (error);
	}
	return (0);
}

int
apple_smc_raw_value_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	char hexbuf[ASMC_MAXVAL * 2 + 1];
	int error, i;

	if (sc->sc_rawkey[0] != '\0') {
		error = apple_smc_key_read(dev, sc->sc_rawkey, sc->sc_rawval,
		    sc->sc_rawlen > 0 ? sc->sc_rawlen : ASMC_MAXVAL);
		if (error != 0) {
			return (error);
		}
	}

	for (i = 0; i < sc->sc_rawlen && i < ASMC_MAXVAL; i++) {
		ksnprintf(hexbuf + i * 2, 3, "%02x", sc->sc_rawval[i]);
	}
	hexbuf[i * 2] = '\0';

	error = sysctl_handle_string(oidp, hexbuf, sizeof(hexbuf), req);
	if (error || req->newptr == NULL) {
		return (error);
	}
	if (sc->sc_rawkey[0] == '\0') {
		return (EINVAL);
	}

	memset(sc->sc_rawval, 0, sizeof(sc->sc_rawval));
	for (i = 0; i < sc->sc_rawlen && hexbuf[i*2] && hexbuf[i*2+1]; i++) {
		unsigned int val;
		char tmp[3] = { hexbuf[i*2], hexbuf[i*2+1], 0 };

		if (sscanf(tmp, "%02x", &val) == 1) {
			sc->sc_rawval[i] = (uint8_t)val;
		}
	}
	return (apple_smc_key_write(dev, sc->sc_rawkey,
	    sc->sc_rawval, sc->sc_rawlen) != 0 ? EIO : 0);
}

int
apple_smc_raw_len_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int v = sc->sc_rawlen;
	return (sysctl_handle_int(oidp, &v, 0, req));
}

int
apple_smc_raw_type_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	return (sysctl_handle_string(oidp, sc->sc_rawtype,
	    sizeof(sc->sc_rawtype), req));
}
#endif /* APPLE_SMC_DEBUG */
