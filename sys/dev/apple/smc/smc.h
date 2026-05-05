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

#ifndef _DEV_APPLE_SMC_SMC_H_
#define _DEV_APPLE_SMC_SMC_H_

#include "opt_apple_smc.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/rman.h>
#include <sys/thread.h>

#include <sys/bus_resource.h>

#include <contrib/dev/acpica/source/include/acpi.h>
#include <contrib/dev/acpica/source/include/accommon.h>

#include <dev/acpica/acpivar.h>

#include "debug.h"

#define SMC_LOCK(sc)	lockmgr(&(sc)->sc_lock, LK_EXCLUSIVE)
#define SMC_UNLOCK(sc)	lockmgr(&(sc)->sc_lock, LK_RELEASE)

#define ASMC_MAXFANS		6
#define ASMC_MAXVAL		32
#define ASMC_KEYLEN		4
#define ASMC_TYPELEN		4
#define ASMC_MAX_SENSORS	64
#define ASMC_TEMP_MAX		80
#define ASMC_MAXRETRIES		10

struct apple_smc_softc {
	device_t		sc_dev;
	struct lock		sc_lock;
	int			sc_nfan;
	int			sc_nkeys;
	int16_t			sms_rest_x;
	int16_t			sms_rest_y;
	int16_t			sms_rest_z;
	struct sysctl_oid	*sc_fan_tree[ASMC_MAXFANS + 1];
	struct sysctl_oid	*sc_temp_tree;
	struct sysctl_oid	*sc_sms_tree;
	struct sysctl_oid	*sc_light_tree;
	int			sc_rid_port;
	int			sc_rid_irq;
	struct resource		*sc_ioport;
	struct resource		*sc_irq;
	/* MMIO backend.  sc_is_mmio: MMIO window is mapped and usable (set for
	 * iMac14,1 and newer).  sc_is_t2: RGEN==3, i.e. Apple T2 security chip
	 * is present.  MMIO can exist without T2 (e.g. iMac14,1 has MMIO but
	 * no T2 coprocessor). */
	int			sc_rid_mem;
	struct resource		*sc_iomem;
	int			sc_is_mmio;
	int			sc_is_t2;
	void			*sc_cookie;
	int			sc_sms_intrtype;
	struct taskqueue	*sc_sms_tq;
	struct task		sc_sms_task;
	uint8_t			sc_sms_intr_works;
	uint32_t		sc_kbd_bkl_level;
#ifdef APPLE_SMC_DEBUG
	struct sysctl_oid	*sc_raw_tree;
	char			sc_rawkey[ASMC_KEYLEN + 1];
	uint8_t			sc_rawval[ASMC_MAXVAL];
	uint8_t			sc_rawlen;
	char			sc_rawtype[ASMC_TYPELEN + 1];
#endif
	char			*sc_voltage_sensors[ASMC_MAX_SENSORS];
	int			sc_voltage_count;
	char			*sc_current_sensors[ASMC_MAX_SENSORS];
	int			sc_current_count;
	char			*sc_power_sensors[ASMC_MAX_SENSORS];
	int			sc_power_count;
	char			*sc_light_sensors[ASMC_MAX_SENSORS];
	int			sc_light_count;
	char			*sc_temp_sensors[ASMC_TEMP_MAX];
	int			sc_temp_count;
	char			*sc_dts_sensors[ASMC_TEMP_MAX];
	int			sc_dts_count;
	int			sc_tj_max;	/* Tj,max in degrees C (from MSR 0x1A2) */
	int			sc_has_sms;
	int			sc_has_light;
	int			sc_light_len;
	int			sc_has_safespeed;
	int			sc_has_alsl;
};

#define ASMC_DATAPORT_READ(sc)		bus_read_1(sc->sc_ioport, 0x00)
#define ASMC_DATAPORT_WRITE(sc, val)	bus_write_1(sc->sc_ioport, 0x00, val)
#define ASMC_CMDPORT_READ(sc)		bus_read_1(sc->sc_ioport, 0x04)
#define ASMC_CMDPORT_WRITE(sc, val)	bus_write_1(sc->sc_ioport, 0x04, val)
#define ASMC_INTPORT_READ(sc)		bus_read_1(sc->sc_ioport, 0x1f)
#define ASMC_STATUS_MASK		0x0f

#define ASMC_CMDREAD		0x10
#define ASMC_CMDWRITE		0x11
#define ASMC_CMDGETBYINDEX	0x12
#define ASMC_CMDGETINFO		0x13

#define ASMC_STATUS_AWAIT_DATA	0x04
#define ASMC_STATUS_DATA_READY	0x05
#define ASMC_KEYINFO_RESPLEN	6

#define ASMC_NKEYS		"#KEY"
#define ASMC_KEY_REV		"REV "
#define ASMC_KEY_FANCOUNT	"FNum"
#define ASMC_KEY_FANMANUAL	"FS! "
#define ASMC_KEY_FANID		"F%dID"
#define ASMC_KEY_FANSPEED	"F%dAc"
#define ASMC_KEY_FANMINSPEED	"F%dMn"
#define ASMC_KEY_FANMAXSPEED	"F%dMx"
#define ASMC_KEY_FANSAFESPEED	"F%dSf"
#define ASMC_KEY_FANTARGETSPEED	"F%dTg"
#define ASMC_KEY_FANMANUAL_T2	"F%dMd"
#define ASMC_KEY_FANSAFESPEED0	"F0Sf"
#define ASMC_SMS_INIT1		0xe0	/* expected init handshake byte 1 */
#define ASMC_SMS_INIT2		0xf8	/* expected init handshake byte 2 */
#define ASMC_KEY_SMS		"MOCN"	/* sudden motion sensor control */
#define ASMC_KEY_SMS_X		"MO_X"	/* accelerometer X axis */
#define ASMC_KEY_SMS_Y		"MO_Y"	/* accelerometer Y axis */
#define ASMC_KEY_SMS_Z		"MO_Z"	/* accelerometer Z axis */
#define ASMC_KEY_SMS_LOW	"MOLT"	/* low-g threshold */
#define ASMC_KEY_SMS_HIGH	"MOHT"	/* high-g threshold */
#define ASMC_KEY_SMS_LOW_INT	"MOLD"	/* low-g interrupt count */
#define ASMC_KEY_SMS_HIGH_INT	"MOHD"	/* high-g interrupt count */
#define ASMC_KEY_SMS_FLAG	"MSDW"	/* SMS enable flag */
#define ASMC_SMS_INTFF		0x60	/* interrupt: free fall */
#define ASMC_SMS_INTHA		0x6f	/* interrupt: high acceleration */
#define ASMC_SMS_INTSH		0x80	/* interrupt: shock */
#define ASMC_ALSL_INT2A		0x2a	/* ambient light sensor interrupt */
#define ASMC_LIGHT_SHORTLEN	6	/* ALV key payload length (6-byte) */
#define ASMC_LIGHT_LONGLEN	10	/* ALV key payload length (10-byte, newer) */
#define ASMC_KEY_LIGHTLEFT	"ALV0"	/* ambient light sensor left */
#define ASMC_KEY_LIGHTRIGHT	"ALV1"	/* ambient light sensor right */
#define ASMC_KEY_LIGHTVALUE	"LKSB"	/* keyboard backlight brightness */
#define ASMC_KEY_LIGHTSRC	"ALSL"	/* ambient light source selector */
#define ASMC_KEY_CLAMSHELL	"MSLD"	/* lid closed flag */
#define ASMC_KEY_AUPO		"AUPO"	/* auto power-on after AC loss */
#define ASMC_KEY_INTOK		"NTOK"	/* interrupt acknowledge */
#define ASMC_MMIO_DATA		0x0000
#define ASMC_MMIO_KEY_NAME	0x0078
#define ASMC_MMIO_DATA_LEN	0x007D
#define ASMC_MMIO_SMC_ID	0x007E
#define ASMC_MMIO_CMD		0x007F
#define ASMC_MMIO_STATUS	0x4005
#define ASMC_MMIO_MIN_SIZE	0x4006
#define ASMC_MMIO_STATUS_READY	0x20
#define ASMC_MMIO_MAX_WAIT	24
#define ASMC_KEY_LDKN		"LDKN"	/* T2 firmware key generation (u8; >=2 = T2) */
#define ASMC_KEY_BCLM		"BCLM"	/* battery charge limit (0-100%) */
#define ASMC_KEY_CLKT		"CLKT"	/* seconds since midnight (time of day) */
#define ASMC_KEY_MSSD		"MSSD"	/* last shutdown cause (signed byte) */
#define ASMC_KEY_MSSP		"MSSP"	/* last sleep cause (signed byte) */
#define ASMC_KEY_MSAL		"MSAL"	/* thermal alert flags; bits 0x04, 0x10, 0x20 reserved/unknown */
#define ASMC_KEY_MSPS		"MSPS"	/* SMC power state index */
#define ASMC_KEY_MSTS		"MSTS"	/* system thermal status */
#define ASMC_KEY_RPLT		"RPlt"	/* board codename (8-byte ASCII) */
#define ASMC_KEY_RGEN		"RGEN"	/* Apple chip generation (3 = T2) */

/* smc_io.c */
int	apple_smc_command(device_t, uint8_t);
int	apple_smc_wait(device_t, uint8_t);
int	apple_smc_key_read(device_t, const char *, uint8_t *, uint8_t);
int	apple_smc_key_write(device_t, const char *, uint8_t *, uint8_t);
int	apple_smc_key_getinfo(device_t, const char *, uint8_t *, char *);
int	apple_smc_fan_count(device_t);
int	apple_smc_fan_getvalue(device_t, const char *, int);
int	apple_smc_fan_setvalue(device_t, const char *, int, int);
char   *apple_smc_fan_getstring(device_t, const char *, int, uint8_t *, uint8_t);
int	apple_smc_temp_getvalue(device_t, const char *);
int	apple_smc_sms_read(device_t, const char *, int16_t *);
void	apple_smc_sms_calibrate(device_t);
void	apple_smc_sms_intr(void *);
void	apple_smc_sms_printintr(device_t, uint8_t);
void	apple_smc_sms_task(void *, int);
void	apple_smc_sms_init(device_t);
int	apple_smc_sensor_read(device_t, const char *, int *);
int	apple_smc_key_dump_by_index(device_t, int, char *, char *, uint8_t *);
int	apple_smc_key_search(device_t, const char *, unsigned int *);
void	apple_smc_scan_sensor_range(device_t, unsigned int, unsigned int,
	    char, int *, char **, int);
int	apple_smc_sensor_type_supported(const char *);

/* smc_mmio.c */
int	apple_smc_try_enable_mmio(device_t);
int	apple_smc_mmio_key_read(device_t, const char *, uint8_t *, uint8_t);
int	apple_smc_mmio_key_write(device_t, const char *, uint8_t *, uint8_t);
int	apple_smc_mmio_key_getinfo(device_t, const char *, uint8_t *, char *);
int	apple_smc_mmio_key_getbyindex(device_t, int, char *);

/* smc_sysctl.c */
unsigned int apple_smc_get_light_control(void);
int	apple_smc_mb_sysctl_fanid(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fanspeed(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fansafespeed(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fanminspeed(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fanmaxspeed(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fantargetspeed(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_fanmanual(SYSCTL_HANDLER_ARGS);
int	apple_smc_temp_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_dts_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_sms_x(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_sms_y(SYSCTL_HANDLER_ARGS);
int	apple_smc_mb_sysctl_sms_z(SYSCTL_HANDLER_ARGS);
int	apple_smc_mbp_sysctl_light_left(SYSCTL_HANDLER_ARGS);
int	apple_smc_mbp_sysctl_light_right(SYSCTL_HANDLER_ARGS);
int	apple_smc_mbp_sysctl_light_control(SYSCTL_HANDLER_ARGS);
int	apple_smc_mbp_sysctl_light_left_10byte(SYSCTL_HANDLER_ARGS);
int	apple_smc_aupo_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_bclm_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_sensor_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_mssd_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_mssp_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_msal_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_clkt_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_msps_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_rplt_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_rgen_sysctl(SYSCTL_HANDLER_ARGS);
#ifdef APPLE_SMC_DEBUG
int	apple_smc_raw_key_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_raw_value_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_raw_len_sysctl(SYSCTL_HANDLER_ARGS);
int	apple_smc_raw_type_sysctl(SYSCTL_HANDLER_ARGS);
#endif

#endif /* _DEV_APPLE_SMC_SMC_H_ */
