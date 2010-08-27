/*-
 * Copyright (c) 1997, 1998
 *	Nan Yang Computer Services Limited.  All rights reserved.
 *
 *  This software is distributed under the so-called ``Berkeley
 *  License'':
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
 *	This product includes software developed by Nan Yang Computer
 *      Services Limited.
 * 4. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *  
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 */

/*
 * Header files used by all modules
 */

#include <sys/param.h>
#ifdef _KERNEL
#include "opt_vinum.h"
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/dkstat.h>
#endif
#include <sys/errno.h>
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/buf.h>
#include <sys/malloc.h>
#endif
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/diskslice.h>
#include <sys/dtype.h>
#include <sys/syslog.h>
#include <sys/fcntl.h>
#include <sys/queue.h>
#include <sys/device.h>
#ifdef _KERNEL
#include <machine/setjmp.h>
#include <machine/stdarg.h>
#include <sys/thread2.h>
#include <sys/buf2.h>
#include <sys/mplock2.h>
#else
#include <setjmp.h>
#include <stdarg.h>
#endif
#include <vm/vm.h>

#include <vfs/ufs/dinode.h>	/* XXX used only by fs.h */
#include <vfs/ufs/fs.h>		/* XXX used only to get SBSIZE and BBSIZE */

#include "vinumvar.h"
#include "vinumio.h"
#include "vinumkw.h"
#include "vinumext.h"
#include <machine/cpu.h>
