/*-
 * Copyright (c) 2001 Mitsuru IWASAKI
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/acpica/acpi_machdep.c,v 1.20 2004/05/05 19:51:15 njl Exp $
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include "acpi.h"
#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpiio.h>

static device_t	acpi_dev;

/*
 * APM driver emulation
 */

#include <sys/event.h>

#include <machine/apm_bios.h>
#include <machine/pc/bios.h>
#include <machine_base/apm/apm.h>
#include <machine_base/apic/ioapic.h>
#include <machine/smp.h>

uint32_t acpi_reset_video = 1;
TUNABLE_INT("hw.acpi.reset_video", &acpi_reset_video);

static struct apm_softc	apm_softc;

static d_open_t apmopen;
static d_close_t apmclose;
static d_write_t apmwrite;
static d_ioctl_t apmioctl;
static d_kqfilter_t apmkqfilter;

static struct dev_ops apm_ops = {
	{ "apm", 0, 0 },
        .d_open = apmopen,
        .d_close = apmclose,
	.d_write = apmwrite,
        .d_ioctl = apmioctl,
	.d_kqfilter = apmkqfilter
};

static int
acpi_capm_convert_battstate(struct  acpi_battinfo *battp)
{
	int	state;

	state = 0xff;	/* XXX unknown */

	if (battp->state & ACPI_BATT_STAT_DISCHARG) {
		if (battp->cap >= 50)
			state = 0;	/* high */
		else
			state = 1;	/* low */
	}
	if (battp->state & ACPI_BATT_STAT_CRITICAL)
		state = 2;		/* critical */
	if (battp->state & ACPI_BATT_STAT_CHARGING)
		state = 3;		/* charging */

	/* If still unknown, determine it based on the battery capacity. */
	if (state == 0xff) {
		if (battp->cap >= 50)
			state = 0;	/* high */
		else
			state = 1;	/* low */
	}

	return (state);
}

static int
acpi_capm_convert_battflags(struct  acpi_battinfo *battp)
{
	int	flags;

	flags = 0;

	if (battp->cap >= 50)
		flags |= APM_BATT_HIGH;
	else {
		if (battp->state & ACPI_BATT_STAT_CRITICAL)
			flags |= APM_BATT_CRITICAL;
		else
			flags |= APM_BATT_LOW;
	}
	if (battp->state & ACPI_BATT_STAT_CHARGING)
		flags |= APM_BATT_CHARGING;
	if (battp->state == ACPI_BATT_STAT_NOT_PRESENT)
		flags = APM_BATT_NOT_PRESENT;

	return (flags);
}

static int
acpi_capm_get_info(apm_info_t aip)
{
	int	acline;
	struct	acpi_battinfo batt;

	aip->ai_infoversion = 1;
	aip->ai_major       = 1;
	aip->ai_minor       = 2;
	aip->ai_status      = apm_softc.active;
	aip->ai_capabilities= 0xff00;	/* XXX unknown */

	if (acpi_acad_get_acline(&acline))
		aip->ai_acline = 0xff;		/* unknown */
	else
		aip->ai_acline = acline;	/* on/off */

	if (acpi_battery_get_battinfo(NULL, &batt)) {
		aip->ai_batt_stat = 0xff;	/* unknown */
		aip->ai_batt_life = 0xff;	/* unknown */
		aip->ai_batt_time = -1;		/* unknown */
		aip->ai_batteries = 0;
	} else {
		aip->ai_batt_stat = acpi_capm_convert_battstate(&batt);
		aip->ai_batt_life = batt.cap;
		aip->ai_batt_time = (batt.min == -1) ? -1 : batt.min * 60;
		aip->ai_batteries = acpi_battery_get_units();
	}

	return (0);
}

static int
acpi_capm_get_pwstatus(apm_pwstatus_t app)
{
	device_t dev;
	int	acline, unit, error;
	struct	acpi_battinfo batt;

	if (app->ap_device != PMDV_ALLDEV &&
	    (app->ap_device < PMDV_BATT0 || app->ap_device > PMDV_BATT_ALL))
		return (1);

	if (app->ap_device == PMDV_ALLDEV)
		error = acpi_battery_get_battinfo(NULL, &batt);
	else {
		unit = app->ap_device - PMDV_BATT0;
		dev = devclass_get_device(devclass_find("battery"), unit);
		if (dev != NULL)
			error = acpi_battery_get_battinfo(dev, &batt);
		else
			error = ENXIO;
	}
	if (error)
		return (1);

	app->ap_batt_stat = acpi_capm_convert_battstate(&batt);
	app->ap_batt_flag = acpi_capm_convert_battflags(&batt);
	app->ap_batt_life = batt.cap;
	app->ap_batt_time = (batt.min == -1) ? -1 : batt.min * 60;

	if (acpi_acad_get_acline(&acline))
		app->ap_acline = 0xff;		/* unknown */
	else
		app->ap_acline = acline;	/* on/off */

	return (0);
}

static int
apmopen(struct dev_open_args *ap)
{
	return (0);
}

static int
apmclose(struct dev_close_args *ap)
{
	return (0);
}

static int
apmioctl(struct dev_ioctl_args *ap)
{
	int	error = 0;
	struct	acpi_softc *acpi_sc;
	struct apm_info info;
	apm_info_old_t aiop;

	acpi_sc = device_get_softc(acpi_dev);

	switch (ap->a_cmd) {
	case APMIO_SUSPEND:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		if (apm_softc.active)
			acpi_SetSleepState(acpi_sc, acpi_sc->acpi_suspend_sx);
		else
			error = EINVAL;
		break;
	case APMIO_STANDBY:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		if (apm_softc.active)
			acpi_SetSleepState(acpi_sc, acpi_sc->acpi_standby_sx);
		else
			error = EINVAL;
		break;
	case APMIO_GETINFO_OLD:
		if (acpi_capm_get_info(&info))
			error = ENXIO;
		aiop = (apm_info_old_t)ap->a_data;
		aiop->ai_major = info.ai_major;
		aiop->ai_minor = info.ai_minor;
		aiop->ai_acline = info.ai_acline;
		aiop->ai_batt_stat = info.ai_batt_stat;
		aiop->ai_batt_life = info.ai_batt_life;
		aiop->ai_status = info.ai_status;
		break;
	case APMIO_GETINFO:
		if (acpi_capm_get_info((apm_info_t)ap->a_data))
			error = ENXIO;
		break;
	case APMIO_GETPWSTATUS:
		if (acpi_capm_get_pwstatus((apm_pwstatus_t)ap->a_data))
			error = ENXIO;
		break;
	case APMIO_ENABLE:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		apm_softc.active = 1;
		break;
	case APMIO_DISABLE:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		apm_softc.active = 0;
		break;
	case APMIO_HALTCPU:
		break;
	case APMIO_NOTHALTCPU:
		break;
	case APMIO_DISPLAY:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		break;
	case APMIO_BIOS:
		if ((ap->a_fflag & FWRITE) == 0)
			return (EPERM);
		bzero(ap->a_data, sizeof(struct apm_bios_arg));
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

static int
apmwrite(struct dev_write_args *ap)
{
	return (ap->a_uio->uio_resid);
}

static void apmfilter_detach(struct knote *);
static int apmfilter(struct knote *, long);

static struct filterops apmfilterops =
	{ FILTEROP_ISFD, NULL, apmfilter_detach, apmfilter };

static int
apmkqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;

	kn->kn_fop = &apmfilterops;
	ap->a_result = 0;
	return (0);
}

static void
apmfilter_detach(struct knote *kn) {}

static int
apmfilter(struct knote *kn, long hint)
{
	return (0);
}

static void
acpi_capm_init(struct acpi_softc *sc)
{
        make_dev(&apm_ops, 0, 0, 5, 0664, "apm");
        make_dev(&apm_ops, 8, 0, 5, 0664, "apm");
	kprintf("Warning: ACPI is disabling APM's device.  You can't run both\n");
}

int
acpi_machdep_init(device_t dev)
{
	struct	acpi_softc *sc;
	int intr_model;

	acpi_dev = dev;
	sc = device_get_softc(acpi_dev);

	/*
	 * XXX: Prevent the PnP BIOS code from interfering with
	 * our own scan of ISA devices.
	 */
#if 0
	PnPBIOStable = NULL;
#endif

	acpi_capm_init(sc);

	acpi_install_wakeup_handler(sc);

	if (ioapic_enable)
		intr_model = ACPI_INTR_APIC;
	else
		intr_model = ACPI_INTR_PIC;

	if (intr_model != ACPI_INTR_PIC)
		acpi_SetIntrModel(intr_model);

	SYSCTL_ADD_UINT(&sc->acpi_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->acpi_sysctl_tree), OID_AUTO,
	    "reset_video", CTLFLAG_RD | CTLFLAG_RW, &acpi_reset_video, 0,
	    "Call the VESA reset BIOS vector on the resume path");

	return (0);
}

int
acpi_machdep_quirks(int *quirks)
{
	return (0);
}

