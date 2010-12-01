/*-
 * Copyright (c) 2006 IronPort Systems
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
 * $FreeBSD: src/sys/dev/mfi/mfi_linux.c,v 1.3 2009/05/20 17:29:21 imp Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/file.h>
#include <sys/mapped_ioctl.h>
#include <sys/proc.h>

#if defined(__x86_64__) /* Assume amd64 wants 32 bit Linux */
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <emulation/linux/i386/linux.h>
#include <emulation/linux/i386/linux_proto.h>
#endif
#include <emulation/linux/linux_ioctl.h>
#include <emulation/linux/linux_util.h>

#include <dev/raid/mfi/mfireg.h>
#include <dev/raid/mfi/mfi_ioctl.h>

static struct ioctl_map_range mfi_linux_ioctl_cmds[] = {
	MAPPED_IOCTL_MAP(MFI_LINUX_CMD, MFI_CMD),
	MAPPED_IOCTL_MAP(MFI_LINUX_CMD_2, MFI_CMD),
	MAPPED_IOCTL_MAP(MFI_LINUX_SET_AEN, MFI_SET_AEN),
	MAPPED_IOCTL_MAP(MFI_LINUX_SET_AEN_2, MFI_SET_AEN),
	MAPPED_IOCTL_MAPF(0 ,0, NULL)
};

static struct ioctl_map_handler mfi_linux_ioctl_handler = {
	&linux_ioctl_map,
	"mfi_linux",
	mfi_linux_ioctl_cmds
};

SYSINIT  (mfi_register,   SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_register_handler, &mfi_linux_ioctl_handler);
SYSUNINIT(mfi_unregister, SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_unregister_handler, &mfi_linux_ioctl_handler);

static int
mfi_linux_modevent(module_t mod, int cmd, void *data)
{
	return (0);
}

DEV_MODULE(mfi_linux, mfi_linux_modevent, NULL);
MODULE_DEPEND(mfi, linux, 1, 1, 1);
