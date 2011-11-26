/* 08 Nov 1998*/
/*
 * cdevmod.c - a sample kld module implementing a character device driver.
 *
 * 08 Nov 1998  Rajesh Vaidheeswarran
 *
 * Copyright (c) 1998 Rajesh Vaidheeswarran
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Rajesh Vaidheeswarran.
 * 4. The name Rajesh Vaidheeswarran may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RAJESH VAIDHEESWARRAN ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE RAJESH VAIDHEESWARRAN BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1993 Terrence R. Lambert.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Terrence R. Lambert.
 * 4. The name Terrence R. Lambert may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TERRENCE R. LAMBERT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE TERRENCE R. LAMBERT BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * $FreeBSD: src/share/examples/kld/cdev/module/cdevmod.c,v 1.3.2.1 2000/10/25 09:02:34 sobomax Exp $
 * $DragonFly: src/share/examples/kld/cdev/module/cdevmod.c,v 1.3 2006/10/18 21:38:23 victor Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/device.h>

#include "cdev.h"

#define CDEV_MAJOR 32

static struct dev_ops my_devops = {
	.head = {
		.name = "cdev",	        /* device name */
		.maj = CDEV_MAJOR,	/* major device number */
		.flags = D_TTY	        /* flags */
	},
	.d_open = mydev_open,
	.d_close = mydev_close,
	.d_read = mydev_read,
	.d_write = mydev_write,
	.d_ioctl = mydev_ioctl,
};

/*
 * Used as the variable that is the reference to our device in devfs...
 * we must keep this variable sane until we call kldunload.
 */
static cdev_t sdev;

/*
 * This function is called each time the module is loaded or unloaded.
 * Since we are a miscellaneous module, we have to provide whatever
 * code is necessary to patch ourselves into the area we are being
 * loaded to change.
 *
 * The stat information is basically common to all modules, so there
 * is no real issue involved with stat; we will leave it lkm_nullcmd(),
 * since we don't have to do anything about it.
 */
static int
cdev_load(module_t mod, int cmd, void *arg)
{
	int err = 0;

	switch (cmd) {
	case MOD_LOAD:
		/* Do any initialization that you should do with the kernel. */
	
		/* If we make it to here, print copyright on console. */
		kprintf("\nSample Loaded kld character device driver\n");
		kprintf("Copyright (c) 1998\n");
		kprintf("Rajesh Vaidheeswarran\n");
		kprintf("All rights reserved\n");

		sdev = make_dev(&my_devops, 0, UID_ROOT, GID_WHEEL, 0600,
		    "cdev");

		/* We obtain a reference to sdev, so we can destroy it later. */
		sdev = reference_dev(sdev);
		break;		/* Success*/

	case MOD_UNLOAD:
		kprintf("Unloaded kld character device driver\n");

		destroy_dev(sdev);
		break;		/* Success*/

	default:	/* We only understand load/unload. */
		err = EINVAL;
		break;
	}

	return (err);
}

/* Now declare the module to the system. */
DEV_MODULE(cdev, cdev_load, NULL);
