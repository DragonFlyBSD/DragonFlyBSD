/*	$NetBSD: usb_quirks.c,v 1.50 2004/06/23 02:30:52 mycroft Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usb_quirks.c,v 1.41.2.4 2006/02/15 22:51:08 iedowse Exp $	*/
/*	$DragonFly: src/sys/bus/usb/usb_quirks.c,v 1.9 2007/11/05 19:09:42 hasso Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <bus/usb/usb.h>
#include <bus/usb/usb_quirks.h>

#ifdef USB_DEBUG
extern int usbdebug;
#endif

#define ANY 0xffff

static const struct usbd_quirk_entry {
	u_int16_t idVendor;
	u_int16_t idProduct;
	u_int16_t bcdDevice;
	struct usbd_quirks quirks;
} usb_quirks[] = {
 /* KYE Niche mouse */
 { .idVendor = 0x0458, .idProduct = 0x0001, .bcdDevice = 0x100,
   .quirks   = { UQ_NO_SET_PROTO}},
 /* Inside Out Networks EdgePort/4 RS232 */
 { .idVendor = 0x1608, .idProduct = 0x0001, .bcdDevice = 0x094,
   .quirks   = { UQ_SWAP_UNICODE}},
 /* Dallas Semiconductor J-6502 speakers */
 { .idVendor = 0x04fa, .idProduct = 0x4201, .bcdDevice = 0x0a2,
   .quirks   = { UQ_BAD_ADC | UQ_AU_NO_XU }},
 /* Altec Lansing ADA70 speakers */
 { .idVendor = 0x04d2, .idProduct = 0x0070, .bcdDevice = 0x103,
   .quirks   = { UQ_BAD_ADC }},
 /* Altec Lansing ASC495 speakers */
 { .idVendor = 0x04d2, .idProduct = 0xff05, .bcdDevice = 0x000,
   .quirks   = { UQ_BAD_AUDIO }},
 /* Qtronix Scorpion-980N keyboard */
 { .idVendor = 0x05c7, .idProduct = 0x2011, .bcdDevice = 0x110,
   .quirks   = { UQ_SPUR_BUT_UP }},
 /* Alcor Micro, Inc. kbd hub */
 { .idVendor = 0x0566, .idProduct = 0x2802, .bcdDevice = 0x001,
   .quirks   = { UQ_SPUR_BUT_UP }},
 /* MCT Corp. hub */
 { .idVendor = 0x0711, .idProduct = 0x0100, .bcdDevice = 0x102,
   .quirks   = { UQ_BUS_POWERED }},
 /* MCT Corp. USB-232 interface */
 { .idVendor = 0x0711, .idProduct = 0x0210, .bcdDevice = 0x102,
   .quirks   = { UQ_BUS_POWERED }},
 /* Metricom Ricochet GS */
 { .idVendor = 0x0870, .idProduct = 0x0001, .bcdDevice = 0x100,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* Sanyo SCP-4900 USB Phone */
 { .idVendor = 0x0474, .idProduct = 0x0701, .bcdDevice = 0x000,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* Texas Instruments UT-USB41 hub */
 { .idVendor = 0x0451, .idProduct = 0x1446, .bcdDevice = 0x110,
   .quirks   = { UQ_POWER_CLAIM }},
 /* Telex Communications Enhanced USB Microphone */
 { .idVendor = 0x0562, .idProduct = 0x0001, .bcdDevice = 0x009,
   .quirks   = { UQ_AU_NO_FRAC }},
 /* Silicon Portals Inc. YAP Phone */
 { .idVendor = 0x1527, .idProduct = 0x0201, .bcdDevice = 0x100,
   .quirks   = { UQ_AU_INP_ASYNC }},

 /*
  * XXX All these HP devices should have a revision number,
  * but I don't know what they are.
  */
 /* HP DeskJet 895C */
 { .idVendor = 0x03f0, .idProduct = 0x0004, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 /* HP DeskJet 880C */
 { .idVendor = 0x03f0, .idProduct = 0x0104, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 /* HP DeskJet 815C */
 { .idVendor = 0x03f0, .idProduct = 0x0204, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 /* HP DeskJet 810C/812C */
 { .idVendor = 0x03f0, .idProduct = 0x0304, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 /* HP DeskJet 830C */
 { .idVendor = 0x03f0, .idProduct = 0x0404, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 /* HP DeskJet 1220C */
 { .idVendor = 0x03f0, .idProduct = 0x0212, .bcdDevice = ANY,
   .quirks   = { UQ_BROKEN_BIDIR }},
 
 /*
  * YAMAHA router's ucdDevice is the version of firmware and
  * often changes.
  */
 /* YAMAHA NetVolante RTA54i Broadband&ISDN Router */
 { .idVendor = 0x0499, .idProduct = 0x4000, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* YAMAHA NetVolante RTA55i Broadband VoIP Router */
 { .idVendor = 0x0499, .idProduct = 0x4004, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* YAMAHA NetVolante RTW65b Broadband Wireless Router */
 { .idVendor = 0x0499, .idProduct = 0x4001, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* YAMAHA NetVolante RTW65i Broadband&ISDN Wireless Router */
 { .idVendor = 0x0499, .idProduct = 0x4002, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},

 /* Qualcomm CDMA Technologies MSM modem */
 { .idVendor = 0x05c6, .idProduct = 0x3196, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* Qualcomm CDMA Technologies MSM phone */
 { .idVendor = 0x1004, .idProduct = 0x6000, .bcdDevice = ANY,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},
 /* SUNTAC U-Cable type A3 */
 { .idVendor = 0x05db, .idProduct = 0x000b, .bcdDevice = 0x100,
   .quirks   = { UQ_ASSUME_CM_OVER_DATA }},

 /* Devices which should be ignored by uhid */
 /* APC Back-UPS Pro 500 */
 { .idVendor = 0x051d, .idProduct = 0x0002, .bcdDevice = ANY,
   .quirks   = { UQ_HID_IGNORE }},
 /* Delorme Publishing Earthmate GPS */
 { .idVendor = 0x1163, .idProduct = 0x0100, .bcdDevice = ANY,
   .quirks   = { UQ_HID_IGNORE }},
 /* MGE UPS Systems ProtectionCenter */
 { .idVendor = 0x0463, .idProduct = 0x0001, .bcdDevice = ANY,
   .quirks   = { UQ_HID_IGNORE }},
 /* MGE UPS Systems ProtectionCenter */
 { .idVendor = 0x0463, .idProduct = 0xffff, .bcdDevice = ANY,
   .quirks   = { UQ_HID_IGNORE }},

 /* Apple usb keyboard */
 { .idVendor = 0x05ac, .idProduct = 0x0221, .bcdDevice = ANY,
   .quirks   = { UQ_NO_SET_PROTO}},

 { 0, 0, 0, { 0 } }
};

const struct usbd_quirks usbd_no_quirk = { 0 };

const struct usbd_quirks *
usbd_find_quirk(usb_device_descriptor_t *d)
{
	const struct usbd_quirk_entry *t;
	u_int16_t vendor = UGETW(d->idVendor);
	u_int16_t product = UGETW(d->idProduct);
	u_int16_t revision = UGETW(d->bcdDevice);

	for (t = usb_quirks; t->idVendor != 0; t++) {
		if (t->idVendor  == vendor &&
		    t->idProduct == product &&
		    (t->bcdDevice == ANY || t->bcdDevice == revision))
			break;
	}
#ifdef USB_DEBUG
	if (usbdebug && t->quirks.uq_flags)
		kprintf("usbd_find_quirk 0x%04x/0x%04x/%x: %d\n",
			  UGETW(d->idVendor), UGETW(d->idProduct),
			  UGETW(d->bcdDevice), t->quirks.uq_flags);
#endif
	return (&t->quirks);
}
