/*-
 * Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>


static	int panic_type = 0;

static int
sysctl_panic_type(SYSCTL_HANDLER_ARGS)
{
        int error, value;
	volatile int i;

        /* Write out the old value. */
        error = SYSCTL_OUT(req, &panic_type, sizeof(int));
        if (error || req->newptr == NULL)
                return (error);

        /* Read in and verify the new value. */
        error = SYSCTL_IN(req, &value, sizeof(int));
        if (error)
                return (error);
        if (value < 0)
                return (EINVAL);
        panic_type = value;

	if (panic_type == 1)
		panic("Test panic type 1");

	if (panic_type == 2)
		i = *(int *)0;	/* Fatal trap */

        return (0);
}


SYSCTL_PROC(_debug, OID_AUTO, panic_type, CTLTYPE_INT|CTLFLAG_RW,
	&panic_type, 0, sysctl_panic_type, "I",
	"Test panic type");


static int
ptest_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
        case MOD_LOAD:
		return(0);
        case MOD_UNLOAD:
		return (0);
        default:
		break;
	}

	return (0);
}

moduledata_t ptest_mdata = {
	"ptest",
	ptest_modevent,
	NULL
};

DECLARE_MODULE(ptest, ptest_mdata, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(ptest, 1);
