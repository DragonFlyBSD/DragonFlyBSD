/*
 * Copyright (c) 2009 The DragonFly Project
 * All rights reserved.
 *
 */

#ifdef _KERNEL
#error "osreldate.h must not be used in the kernel, use sys/param.h"
#else
#undef __DragonFly_version
#define __DragonFly_version 200500
#ifdef __FreeBSD__
#undef __FreeBSD_version
#define __FreeBSD_version 480101
#endif
#endif
