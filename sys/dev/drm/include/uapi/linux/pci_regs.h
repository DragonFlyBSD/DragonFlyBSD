/*
 * Copyright (c) 2019 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UAPI_LINUX_PCIREGS_H
#define UAPI_LINUX_PCIREGS_H

#define PCI_EXP_RTCAP		30

#define PCI_EXP_FLAGS		2
#define PCI_EXP_FLAGS_VERS	0x000f
#define PCI_EXP_FLAGS_TYPE	0x00f0

#define PCI_EXP_DEVCAP		4

#define PCI_EXP_LNKCAP		12

#define PCI_EXP_DEVCTL		8
#define PCI_EXP_DEVCTL2		40

#define PCI_EXP_LNKSTA		18
#define PCI_EXP_LNKSTA2		50

#define PCI_EXP_SLTCAP		20

#define PCI_CAP_ID_EXP		0x10

#define PCI_EXP_TYPE_ENDPOINT	0x0
#define PCI_EXP_TYPE_LEG_END	0x1
#define PCI_EXP_TYPE_ROOT_PORT	0x4
#define PCI_EXP_TYPE_DOWNSTREAM 0x6
#define PCI_EXP_TYPE_RC_EC	0xa

#define PCI_EXP_SLTCTL		24

#define PCI_EXP_SLTSTA		26

#define PCI_EXP_FLAGS_SLOT	0x0100

#define PCI_EXP_RTCTL		28

#define PCI_EXP_RTSTA		32

#define PCI_EXP_DEVCAP2		36

#define PCI_EXP_LNKCAP2		44

#define PCI_EXP_LNKCAP_MLW	0x000003f0

#endif /* UAPI_LINUX_PCIREGS_H */
