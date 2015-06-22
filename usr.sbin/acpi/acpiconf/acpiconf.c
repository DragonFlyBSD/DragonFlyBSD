/*-
 * Copyright (c) 1999 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *	$Id: acpiconf.c,v 1.5 2000/08/08 14:12:19 iwasaki Exp $
 *	$FreeBSD: head/usr.sbin/acpi/acpiconf/acpiconf.c 211763 2010-08-24 18:07:59Z mav $
 */

#include <sys/param.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sysexits.h>
#include <unistd.h>

#include "acpiio.h"
#include "acpi.h"

#define ACPIDEV		"/dev/acpi"

static int	acpifd;

static void
acpi_init(void)
{
	acpifd = open(ACPIDEV, O_RDWR);
	if (acpifd == -1)
		acpifd = open(ACPIDEV, O_RDONLY);
	if (acpifd == -1)
		err(EX_OSFILE, ACPIDEV);
}

/* Prepare to sleep and then wait for the signal that sleeping can occur. */
static void
acpi_sleep(int sleep_type)
{
	int ret;

	/* Notify OS that we want to sleep.  devd(8) gets this notify. */
	ret = ioctl(acpifd, ACPIIO_REQSLPSTATE, &sleep_type);
	if (ret != 0)
		err(EX_IOERR, "request sleep type (%d) failed", sleep_type);
}

/* Ack or abort a pending suspend request. */
static void
acpi_sleep_ack(int err_val)
{
	int ret;

	ret = ioctl(acpifd, ACPIIO_ACKSLPSTATE, &err_val);
	if (ret != 0)
		err(EX_IOERR, "ack sleep type failed");
}

/* should be a acpi define, but doesn't appear to be */
#define UNKNOWN_CAP 0xffffffff
#define UNKNOWN_VOLTAGE 0xffffffff

static int
acpi_battinfo(int num)
{
	union acpi_battery_ioctl_arg battio;
	const char *pwr_units;
	int hours, min, amp;
	uint32_t volt;

	if (num < 0 || num > 64)
		err(EX_USAGE, "invalid battery %d", num);

	/* Print battery design information. */
	battio.unit = num;
	if (ioctl(acpifd, ACPIIO_BATT_GET_BIF, &battio) == -1)
		err(EX_IOERR, "get battery info (%d) failed", num);
	amp = battio.bif.units;
	pwr_units = amp ? "mA" : "mW";
	if (battio.bif.dcap == UNKNOWN_CAP)
		printf("Design capacity:\tunknown\n");
	else
		printf("Design capacity:\t%d %sh\n", battio.bif.dcap,
		    pwr_units);
	if (battio.bif.lfcap == UNKNOWN_CAP)
		printf("Last full capacity:\tunknown\n");
	else
		printf("Last full capacity:\t%d %sh\n", battio.bif.lfcap,
		    pwr_units);
	printf("Technology:\t\t%s\n", battio.bif.btech == 0 ?
	    "primary (non-rechargeable)" : "secondary (rechargeable)");
	if (battio.bif.dvol == UNKNOWN_CAP)
		printf("Design voltage:\t\tunknown\n");
	else
		printf("Design voltage:\t\t%d mV\n", battio.bif.dvol);
	printf("Capacity (warn):\t%d %sh\n", battio.bif.wcap, pwr_units);
	printf("Capacity (low):\t\t%d %sh\n", battio.bif.lcap, pwr_units);
	printf("Low/warn granularity:\t%d %sh\n", battio.bif.gra1, pwr_units);
	printf("Warn/full granularity:\t%d %sh\n", battio.bif.gra2, pwr_units);
	printf("Model number:\t\t%s\n", battio.bif.model);
	printf("Serial number:\t\t%s\n", battio.bif.serial);
	printf("Type:\t\t\t%s\n", battio.bif.type);
	printf("OEM info:\t\t%s\n", battio.bif.oeminfo);

	/* Fetch battery voltage information. */
	volt = UNKNOWN_VOLTAGE;
	battio.unit = num;
	if (ioctl(acpifd, ACPIIO_BATT_GET_BST, &battio) == -1)
		err(EX_IOERR, "get battery status (%d) failed", num);
	if (battio.bst.state != ACPI_BATT_STAT_NOT_PRESENT)
		volt = battio.bst.volt;

	/* Print current battery state information. */
	battio.unit = num;
	if (ioctl(acpifd, ACPIIO_BATT_GET_BATTINFO, &battio) == -1)
		err(EX_IOERR, "get battery user info (%d) failed", num);
	if (battio.battinfo.state != ACPI_BATT_STAT_NOT_PRESENT) {
		printf("State:\t\t\t");
		if (battio.battinfo.state == 0)
			printf("high ");
		if (battio.battinfo.state & ACPI_BATT_STAT_CRITICAL)
			printf("critical ");
		if (battio.battinfo.state & ACPI_BATT_STAT_DISCHARG)
			printf("discharging ");
		if (battio.battinfo.state & ACPI_BATT_STAT_CHARGING)
			printf("charging ");
		printf("\n");
		if (battio.battinfo.cap == -1)
			printf("Remaining capacity:\tunknown\n");
		else
			printf("Remaining capacity:\t%d%%\n",
			    battio.battinfo.cap);
		if (battio.battinfo.min == -1)
			printf("Remaining time:\t\tunknown\n");
		else {
			hours = battio.battinfo.min / 60;
			min = battio.battinfo.min % 60;
			printf("Remaining time:\t\t%d:%02d\n", hours, min);
		}
		if (battio.battinfo.rate == -1)
			printf("Present rate:\t\tunknown\n");
		else if (amp && volt != UNKNOWN_VOLTAGE) {
			printf("Present rate:\t\t%d mA (%d mW)\n",
			    battio.battinfo.rate,
			    battio.battinfo.rate * volt / 1000);
		} else
			printf("Present rate:\t\t%d %s\n",
			    battio.battinfo.rate, pwr_units);
	} else
		printf("State:\t\t\tnot present\n");

	/* Print battery voltage information. */
	if (volt == UNKNOWN_VOLTAGE)
		printf("Present voltage:\tunknown\n");
	else
		printf("Present voltage:\t%d mV\n", volt);

	return (0);
}

static void
usage(const char* prog)
{
	printf("usage: %s [-h] [-i batt] [-k ack] [-s 1-4]\n", prog);
	exit(0);
}

int
main(int argc, char *argv[])
{
	char	c, *prog, *ptr;
	int	sleep_type;

	prog = argv[0];
	if (strstr((ptr = strrchr(prog, '/')) ? ptr + 1 : prog, "apm") != 0) {
		acpi_init();
		acpi_battinfo(0);
		close(acpifd);
		exit(0);
	} else if (argc < 2) {
		usage(prog);
		/* NOTREACHED */
	}

	sleep_type = -1;
	acpi_init();
	while ((c = getopt(argc, argv, "hi:k:s:")) != -1) {
		switch (c) {
		case 'i':
			acpi_battinfo(atoi(optarg));
			break;
		case 'k':
			acpi_sleep_ack(atoi(optarg));
			break;
		case 's':
			if (optarg[0] == 'S')
				sleep_type = optarg[1] - '0';
			else
				sleep_type = optarg[0] - '0';
			if (sleep_type < 1 || sleep_type > 4)
				errx(EX_USAGE, "invalid sleep type (%d)",
				     sleep_type);
			break;
		case 'h':
		default:
			usage(prog);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (sleep_type != -1)
		acpi_sleep(sleep_type);

	close(acpifd);
	exit (0);
}
