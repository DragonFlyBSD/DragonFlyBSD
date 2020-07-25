/*
 * Copyright (c) 1997 John S. Dyson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. John S. Dyson's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * DISCLAIMER:  This code isn't warranted to do anything useful.  Anything
 * bad that happens because of using this software isn't the responsibility
 * of the author.  This software is distributed AS-IS.
 *
 * $FreeBSD: src/sys/kern/vfs_aio.c,v 1.70.2.28 2003/05/29 06:15:35 alc Exp $
 */

/*
 * This file contains stubs for the POSIX 1003.1B AIO/LIO facility.
 */

#include <sys/param.h>
#include <sys/sysmsg.h>
#include <sys/event.h>

int
sys_aio_return(struct sysmsg *sysmsg, const struct aio_return_args *uap)
{
	return ENOSYS;
}

int
sys_aio_suspend(struct sysmsg *sysmsg, const struct aio_suspend_args *uap)
{
	return ENOSYS;
}

int
sys_aio_cancel(struct sysmsg *sysmsg, const struct aio_cancel_args *uap)
{
	return ENOSYS;
}

int
sys_aio_error(struct sysmsg *sysmsg, const struct aio_error_args *uap)
{
	return ENOSYS;
}

int
sys_aio_read(struct sysmsg *sysmsg, const struct aio_read_args *uap)
{
	return ENOSYS;
}

int
sys_aio_write(struct sysmsg *sysmsg, const struct aio_write_args *uap)
{
	return ENOSYS;
}

int
sys_lio_listio(struct sysmsg *sysmsg, const struct lio_listio_args *uap)
{
	return ENOSYS;
}

int
sys_aio_waitcomplete(struct sysmsg *sysmsg, const struct aio_waitcomplete_args *uap)
{
	return ENOSYS;
}

static int
filt_aioattach(struct knote *kn)
{
	return ENXIO;
}

struct filterops aio_filtops =
	{ FILTEROP_MPSAFE, filt_aioattach, NULL, NULL };
