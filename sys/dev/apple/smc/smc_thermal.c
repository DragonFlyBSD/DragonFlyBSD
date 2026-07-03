/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Thermal fan governor -- linear fan curve for CPU temperature when
 * the driver has overridden SMC fan control via PROCHOT override.
 */

#include "smc.h"

#include <sys/kthread.h>

#define THERMAL_MIN_INTERVAL	1	/* minimum poll interval (seconds) */
#define THERMAL_MAX_INTERVAL	60	/* maximum poll interval (seconds) */
#define THERMAL_MIN_TEMP	10000	/* 10C -- reject implausible readings */
#define THERMAL_RAMP_DOWN_MAX	200	/* max RPM decrease per tick */
#define THERMAL_WRITE_DELTA	100	/* min RPM change to bother writing */

/*
 * Set all fans to maximum RPM.  Fail-hot / emergency path.
 */
int
apple_smc_thermal_fans_to_max(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int i, max_rpm, ok = 0;

	/* Reassert manual mode. */
	if (apple_smc_fan_reassert_manual(dev) != 0)
		device_printf(dev,
		    "thermal: WARNING: manual mode reassert failed\n");

	/* Write max to every fan. */
	for (i = 0; i < sc->sc_nfan; i++) {
		max_rpm = apple_smc_fan_getvalue(dev, ASMC_KEY_FANMAXSPEED, i);
		if (max_rpm <= 0)
			continue;
		if (apple_smc_fan_setvalue(dev, ASMC_KEY_FANTARGETSPEED,
		    i, max_rpm) == 0)
			ok++;
	}
	return (ok);
}

/*
 * Read hottest CPU core temperature in millidegrees.  Returns -1 on failure.
 */
static int
apple_smc_thermal_read_cpu_temp(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int val, best, i, error;

	best = -1;

	/* TC0c is CPU package composite -- try first. */
	error = apple_smc_sensor_read(dev, "TC0c", &val);
	if (error == 0 && val > THERMAL_MIN_TEMP && val < 130000)
		best = val;

	/* Scan TC* sensors for hottest core. */
	for (i = 0; i < sc->sc_temp_count; i++) {
		if (sc->sc_temp_sensors[i][0] != 'T' ||
		    sc->sc_temp_sensors[i][1] != 'C')
			continue;
		error = apple_smc_sensor_read(dev,
		    sc->sc_temp_sensors[i], &val);
		if (error == 0 && val > best && val > THERMAL_MIN_TEMP &&
		    val < 130000)
			best = val;
	}
	return (best);
}

/*
 * Linear fan curve: min_rpm at temp_low, max_rpm at temp_high.
 */
static int
apple_smc_thermal_compute_rpm(int temp_mc, int temp_low_mc, int temp_high_mc,
    int min_rpm, int max_rpm)
{
	int64_t range, delta, result;

	if (temp_high_mc <= temp_low_mc)
		return (max_rpm);	/* misconfigured -- fail hot */
	if (temp_mc <= temp_low_mc)
		return (min_rpm);
	if (temp_mc >= temp_high_mc)
		return (max_rpm);

	range = (int64_t)(temp_high_mc - temp_low_mc);
	delta = (int64_t)(temp_mc - temp_low_mc);
	result = (int64_t)min_rpm +
	    ((int64_t)(max_rpm - min_rpm) * delta) / range;

	/* Clamp. */
	if (result < min_rpm)
		return (min_rpm);
	if (result > max_rpm)
		return (max_rpm);
	return ((int)result);
}

/*
 * Thermal governor thread.
 */
static void
apple_smc_thermal_thread(void *arg)
{
	device_t dev = (device_t)arg;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int temp_mc, target_rpm, cur_target, i;
	int min_rpm, max_rpm;
	int last_temp = 0;
	int sensor_fail_logged = 0;
	int interval;
	/* Snapshot tunables. */
	int s_temp_low, s_temp_high, s_hysteresis, s_emergency;

	while (!sc->sc_thermal_stop) {
		interval = sc->sc_thermal_interval;
		if (interval < THERMAL_MIN_INTERVAL)
			interval = THERMAL_MIN_INTERVAL;
		if (interval > THERMAL_MAX_INTERVAL)
			interval = THERMAL_MAX_INTERVAL;

		if (!sc->sc_thermal_enabled) {
			tsleep(&sc->sc_thermal_enabled, 0, "smcthw",
			    hz * interval);
			continue;
		}

		/* Snapshot. */
		s_temp_low = sc->sc_thermal_temp_low;
		s_temp_high = sc->sc_thermal_temp_high;
		s_hysteresis = sc->sc_thermal_hysteresis;
		s_emergency = sc->sc_thermal_emergency;

		temp_mc = apple_smc_thermal_read_cpu_temp(dev);

		if (temp_mc < 0) {
			/* Fail hot. */
			apple_smc_thermal_fans_to_max(dev);
			if (!sensor_fail_logged) {
				device_printf(dev,
				    "thermal: sensor read failed, "
				    "fans to max\n");
				sensor_fail_logged = 1;
			}
			tsleep(&sc->sc_thermal_stop, 0, "smcthf",
			    hz * interval);
			continue;
		}
		sensor_fail_logged = 0;

		/* Emergency threshold. */
		if (temp_mc >= s_emergency) {
			if (last_temp < s_emergency)
				device_printf(dev,
				    "thermal: EMERGENCY %d.%03dC, "
				    "fans to max\n",
				    temp_mc / 1000, temp_mc % 1000);
			apple_smc_thermal_fans_to_max(dev);
			last_temp = temp_mc;
			tsleep(&sc->sc_thermal_stop, 0, "smcthe",
			    hz * interval);
			continue;
		}

		/* Reassert manual mode; SMC reclaims auto periodically. */
		apple_smc_fan_reassert_manual(dev);

		for (i = 0; i < sc->sc_nfan; i++) {
			/* Only govern fans we've overridden. */
			if (!(sc->sc_fan_manual_mask & (1 << i)))
				continue;

			min_rpm = apple_smc_fan_getvalue(dev,
			    ASMC_KEY_FANMINSPEED, i);
			max_rpm = apple_smc_fan_getvalue(dev,
			    ASMC_KEY_FANMAXSPEED, i);
			if (min_rpm <= 0 || max_rpm <= 0 || max_rpm < min_rpm)
				continue;

			target_rpm = apple_smc_thermal_compute_rpm(
			    temp_mc, s_temp_low, s_temp_high,
			    min_rpm, max_rpm);

			cur_target = apple_smc_fan_getvalue(dev,
			    ASMC_KEY_FANTARGETSPEED, i);

			/* Hysteresis + ramp-down limit. */
			if (cur_target > 0) {
				int delta = target_rpm - cur_target;

				/* Skip trivial. */
				if (abs(delta) < THERMAL_WRITE_DELTA &&
				    abs(temp_mc - last_temp) < s_hysteresis)
					continue;

				/* Slow ramp-down. */
				if (delta < -THERMAL_RAMP_DOWN_MAX)
					target_rpm =
					    cur_target - THERMAL_RAMP_DOWN_MAX;
			}

			apple_smc_fan_setvalue(dev,
			    ASMC_KEY_FANTARGETSPEED, i, target_rpm);
		}

		last_temp = temp_mc;
		tsleep(&sc->sc_thermal_stop, 0, "smcth",
		    hz * interval);
	}

	sc->sc_thermal_running = 0;
	wakeup(&sc->sc_thermal_running);
}

int
apple_smc_thermal_start(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	int error;

	sc->sc_thermal_stop = 0;
	sc->sc_thermal_running = 0;

	error = kthread_create(apple_smc_thermal_thread, dev,
	    &sc->sc_thermal_td, "apple_smc_thermal");
	if (error == 0)
		sc->sc_thermal_running = 1;
	return (error);
}

void
apple_smc_thermal_stop(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);

	if (!sc->sc_thermal_running)
		return;

	sc->sc_thermal_stop = 1;
	wakeup(&sc->sc_thermal_enabled);
	wakeup(&sc->sc_thermal_stop);

	/* Wait for thread to exit. */
	while (sc->sc_thermal_running)
		tsleep(&sc->sc_thermal_running, 0, "smcthx", hz);

	sc->sc_thermal_td = NULL;
}

/* Sysctl handler for thermal.enabled. */
int
apple_smc_thermal_enabled_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct apple_smc_softc *sc = device_get_softc(dev);
	int val, error;

	val = sc->sc_thermal_enabled;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);

	val = (val != 0) ? 1 : 0;
	if (val == sc->sc_thermal_enabled)
		return (0);

	sc->sc_thermal_enabled = val;

	if (!val && sc->sc_prochot_override) {
		apple_smc_thermal_fans_to_max(dev);
		device_printf(dev,
		    "thermal: governor disabled, fans set to max\n");
	}

	wakeup(&sc->sc_thermal_enabled);
	return (0);
}

int
apple_smc_thermal_int_sysctl(SYSCTL_HANDLER_ARGS)
{
	int *valp = (int *)arg1;
	int val, error;

	val = *valp;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (val <= 0)
		return (EINVAL);
	*valp = val;
	return (0);
}
