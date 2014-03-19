/*-
 * Copyright (c) 2002 Scott Long
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
 * $FreeBSD: head/sys/dev/aac/aac_linux.c 165393 2006-12-20 17:10:53Z delphij $
 */

/*
 * Linux ioctl handler for the aac device driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/file.h>
#include <sys/mapped_ioctl.h>
#include <sys/proc.h>
#ifdef __x86_64__
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <emulation/linux/i386/linux.h>
#include <emulation/linux/i386/linux_proto.h>
#endif
#include <emulation/linux/linux_ioctl.h>

#include <dev/raid/aac/aacreg.h>	/* needed by aac_ioctl.h */
#include <dev/raid/aac/aac_ioctl.h>

/* Define ioctl mappings */
static struct ioctl_map_range aac_linux_ioctl_cmds[] = {
	MAPPED_IOCTL_MAP(FSACTL_LNX_SENDFIB, FSACTL_SENDFIB),
	MAPPED_IOCTL_MAP(FSACTL_LNX_GET_COMM_PERF_DATA, FSACTL_GET_COMM_PERF_DATA),
	MAPPED_IOCTL_MAP(FSACTL_LNX_OPENCLS_COMM_PERF_DATA, FSACTL_OPENCLS_COMM_PERF_DATA),
	MAPPED_IOCTL_MAP(FSACTL_LNX_OPEN_GET_ADAPTER_FIB, FSACTL_OPEN_GET_ADAPTER_FIB),
	MAPPED_IOCTL_MAP(FSACTL_LNX_GET_NEXT_ADAPTER_FIB, FSACTL_GET_NEXT_ADAPTER_FIB),
	MAPPED_IOCTL_MAP(FSACTL_LNX_CLOSE_GET_ADAPTER_FIB, FSACTL_CLOSE_GET_ADAPTER_FIB),
	MAPPED_IOCTL_MAP(FSACTL_LNX_CLOSE_ADAPTER_CONFIG, FSACTL_CLOSE_ADAPTER_CONFIG),
	MAPPED_IOCTL_MAP(FSACTL_LNX_OPEN_ADAPTER_CONFIG, FSACTL_OPEN_ADAPTER_CONFIG),
	MAPPED_IOCTL_MAP(FSACTL_LNX_MINIPORT_REV_CHECK, FSACTL_MINIPORT_REV_CHECK),
	MAPPED_IOCTL_MAP(FSACTL_LNX_QUERY_ADAPTER_CONFIG, FSACTL_QUERY_ADAPTER_CONFIG),
	MAPPED_IOCTL_MAP(FSACTL_LNX_GET_PCI_INFO, FSACTL_GET_PCI_INFO),
	MAPPED_IOCTL_MAP(FSACTL_LNX_FORCE_DELETE_DISK, FSACTL_FORCE_DELETE_DISK),
	MAPPED_IOCTL_MAP(FSACTL_LNX_AIF_THREAD, FSACTL_AIF_THREAD),
	MAPPED_IOCTL_MAP(FSACTL_LNX_NULL_IO_TEST, FSACTL_NULL_IO_TEST),
	MAPPED_IOCTL_MAP(FSACTL_LNX_SIM_IO_TEST, FSACTL_SIM_IO_TEST),
	MAPPED_IOCTL_MAP(FSACTL_LNX_DOWNLOAD, FSACTL_DOWNLOAD),
	MAPPED_IOCTL_MAP(FSACTL_LNX_GET_VAR, FSACTL_GET_VAR),
	MAPPED_IOCTL_MAP(FSACTL_LNX_SET_VAR, FSACTL_SET_VAR),
	MAPPED_IOCTL_MAP(FSACTL_LNX_GET_FIBTIMES, FSACTL_GET_FIBTIMES),
	MAPPED_IOCTL_MAP(FSACTL_LNX_ZERO_FIBTIMES, FSACTL_ZERO_FIBTIMES),
	MAPPED_IOCTL_MAP(FSACTL_LNX_DELETE_DISK, FSACTL_DELETE_DISK),
	MAPPED_IOCTL_MAP(FSACTL_LNX_QUERY_DISK, FSACTL_QUERY_DISK),
	MAPPED_IOCTL_MAP(FSACTL_LNX_PROBE_CONTAINERS, FSACTL_PROBE_CONTAINERS),
	MAPPED_IOCTL_MAPF(0 ,0, NULL)
};

static struct ioctl_map_handler aac_linux_ioctl_handler = {
	&linux_ioctl_map,
	"aac_linux",
	aac_linux_ioctl_cmds
};

SYSINIT  (aac_register,   SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_register_handler, &aac_linux_ioctl_handler);
SYSUNINIT(aac_unregister, SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_unregister_handler, &aac_linux_ioctl_handler);

static int
aac_linux_modevent(module_t mod, int type, void *data)
{
	/* Do we care about any specific load/unload actions? */
	return (0);
}

DEV_MODULE(aac_linux, aac_linux_modevent, NULL);
MODULE_DEPEND(aac_linux, linux, 1, 1, 1);
