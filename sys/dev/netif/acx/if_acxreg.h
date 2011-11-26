/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/acx/if_acxreg.h,v 1.1 2006/04/01 02:55:36 sephe Exp $
 */

#ifndef _IF_ACXREG_H
#define _IF_ACXREG_H

/*
 * IO register index
 */
#define ACXREG_SOFT_RESET		0
#define ACXREG_FWMEM_ADDR		1
#define ACXREG_FWMEM_DATA		2
#define ACXREG_FWMEM_CTRL		3
#define ACXREG_FWMEM_START		4
#define ACXREG_EVENT_MASK		5
#define ACXREG_INTR_TRIG		6
#define ACXREG_INTR_MASK		7
#define ACXREG_INTR_STATUS		8
#define ACXREG_INTR_STATUS_CLR		9	/* cleared after being read */
#define ACXREG_INTR_ACK			10
#define ACXREG_HINTR_TRIG		11	/* XXX what's this? */
#define ACXREG_RADIO_ENABLE		12
#define ACXREG_EEPROM_INIT		13
#define ACXREG_EEPROM_CTRL		14
#define ACXREG_EEPROM_ADDR		15
#define ACXREG_EEPROM_DATA		16
#define ACXREG_EEPROM_CONF		17
#define ACXREG_EEPROM_INFO		18
#define ACXREG_PHY_ADDR			19
#define ACXREG_PHY_DATA			20
#define ACXREG_PHY_CTRL			21
#define ACXREG_GPIO_OUT_ENABLE		22
#define ACXREG_GPIO_OUT			23
#define ACXREG_CMD_REG_OFFSET		24
#define ACXREG_INFO_REG_OFFSET		25
#define ACXREG_RESET_SENSE		26
#define ACXREG_ECPU_CTRL		27
#define ACXREG_MAX			28
#define ACXREG(reg, val)		[ACXREG_##reg] = val

/*
 * Value read from ACXREG_EEPROM_INFO
 * upper 8bits are radio type
 * lower 8bits are form factor
 */
#define ACX_EEINFO_RADIO_TYPE_SHIFT	8
#define ACX_EEINFO_RADIO_TYPE_MASK	(0xff << ACX_EEINFO_RADIO_TYPE_SHIFT)
#define ACX_EEINFO_FORM_FACTOR_MASK	0xff

#define ACX_EEINFO_HAS_RADIO_TYPE(info)	((info) & ACX_EEINFO_RADIO_TYPE_MASK)
#define ACX_EEINFO_RADIO_TYPE(info)	((info) >> ACX_EEINFO_RADIO_TYPE_SHIFT)
#define ACX_EEINFO_FORM_FACTOR(info)	((info) & ACX_EEINFO_FORM_FACTOR_MASK)

/*
 * Size of command register whose location is obtained
 * from ACXREG_CMD_REG_OFFSET IO register
 */
#define ACX_CMD_REG_SIZE		4	/* 4 bytes */

/*
 * Size of infomation register whose location is obtained
 * from ACXREG_INFO_REG_OFFSET IO register
 */
#define ACX_INFO_REG_SIZE		4	/* 4 bytes */

/*
 * Offset of EEPROM variables
 */
#define ACX_EE_VERSION_OFS		0x05

/*
 * Possible values for various IO registers
 */

/* ACXREG_SOFT_RESET */
#define ACXRV_SOFT_RESET		0x1

/* ACXREG_FWMEM_START */
#define ACXRV_FWMEM_START_OP		0x0

/* ACXREG_FWMEM_CTRL */
#define ACXRV_FWMEM_ADDR_AUTOINC	0x10000

/* ACXREG_EVENT_MASK */
#define ACXRV_EVENT_DISABLE		0x8000	/* XXX What's this?? */

/* ACXREG_INTR_TRIG */
#define ACXRV_TRIG_CMD_FINI		0x0001
#define ACXRV_TRIG_TX_FINI		0x0004

/* ACXREG_INTR_MASK */
#define ACXRV_INTR_RX_DATA		0x0001
#define ACXRV_INTR_TX_FINI		0x0002
#define ACXRV_INTR_TX_XFER		0x0004
#define ACXRV_INTR_RX_FINI		0x0008
#define ACXRV_INTR_DTIM			0x0010
#define ACXRV_INTR_BEACON		0x0020
#define ACXRV_INTR_TIMER		0x0040
#define ACXRV_INTR_KEY_MISS		0x0080
#define ACXRV_INTR_WEP_FAIL		0x0100
#define ACXRV_INTR_CMD_FINI		0x0200
#define ACXRV_INTR_INFO			0x0400
#define ACXRV_INTR_OVERFLOW		0x0800	/* XXX */
#define ACXRV_INTR_PROC_ERR		0x1000	/* XXX */
#define ACXRV_INTR_SCAN_FINI		0x2000
#define ACXRV_INTR_FCS_THRESH		0x4000	/* XXX */
#define ACXRV_INTR_UNKN			0x8000
#define ACXRV_INTR_ALL			0xffff

/* ACXREG_EEPROM_INIT */
#define ACXRV_EEPROM_INIT		0x1

/* ACXREG_EEPROM_CTRL */
#define ACXRV_EEPROM_READ		0x2

/* ACXREG_PHY_CTRL */
#define ACXRV_PHY_WRITE			0x1
#define ACXRV_PHY_READ			0x2

/* ACXREG_PHY_ADDR */
#define ACXRV_PHYREG_TXPOWER		0x11	/* axc100 */
#define ACXRV_PHYREG_SENSITIVITY	0x30

/* ACXREG_ECPU_CTRL */
#define ACXRV_ECPU_HALT			0x1
#define ACXRV_ECPU_START		0x0

#endif	/* !_IF_ACXREG_H */
