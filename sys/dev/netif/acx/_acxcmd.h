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
 * $DragonFly: src/sys/dev/netif/acx/_acxcmd.h,v 1.2 2008/06/06 10:47:14 sephe Exp $
 */

#ifndef _X_ACXCMD_H
#define _X_ACXCMD_H

#define ACXCMD_GET_CONF		0x01
#define ACXCMD_SET_CONF		0x02
#define ACXCMD_ENABLE_RXCHAN	0x03
#define ACXCMD_ENABLE_TXCHAN	0x04
#define ACXCMD_TMPLT_TIM	0x0a
#define ACXCMD_JOIN_BSS		0x0b
#define ACXCMD_WEP_MGMT		0x0c	/* acx111 */
#define ACXCMD_SLEEP		0x0f
#define ACXCMD_WAKEUP		0x10
#define ACXCMD_INIT_MEM		0x12	/* acx100 */
#define ACXCMD_TMPLT_BEACON	0x13
#define ACXCMD_TMPLT_PROBE_RESP	0x14
#define ACXCMD_TMPLT_NULL_DATA	0x15
#define ACXCMD_TMPLT_PROBE_REQ	0x16
#define ACXCMD_INIT_RADIO	0x18
#define ACXCMD_CALIBRATE	0x19	/* acx111 */

#if 0
/*
 * acx111 does not agree with acx100 about
 * the meaning of following values.  So they
 * are put into chip specific files.
 */
#define ACX_CONF_FW_RING	0x0003
#define ACX_CONF_MEMOPT		0x0005
#endif
#define ACX_CONF_MEMBLK_SIZE	0x0004	/* acx100 */
#define ACX_CONF_RATE_FALLBACK	0x0006
#define ACX_CONF_WEPOPT		0x0007	/* acx100 */
#define ACX_CONF_MMAP		0x0008
#define ACX_CONF_FWREV		0x000d
#define ACX_CONF_RXOPT		0x0010
#define ACX_CONF_OPTION		0x0015	/* acx111 */
#define ACX_CONF_EADDR		0x1001
#define ACX_CONF_NRETRY_SHORT	0x1005
#define ACX_CONF_NRETRY_LONG	0x1006
#define ACX_CONF_WEPKEY		0x1007	/* acx100 */
#define ACX_CONF_MSDU_LIFETIME	0x1008
#define ACX_CONF_REGDOM		0x100a
#define ACX_CONF_ANTENNA	0x100b
#define ACX_CONF_TXPOWER	0x100d	/* acx111 */
#define ACX_CONF_CCA_MODE	0x100e
#define ACX_CONF_ED_THRESH	0x100f
#define ACX_CONF_WEP_TXKEY	0x1010

#endif	/* !_X_ACXCMD_H */
