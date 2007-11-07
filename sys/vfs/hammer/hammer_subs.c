/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/vfs/hammer/hammer_subs.c,v 1.2 2007/11/07 00:43:24 dillon Exp $
 */
/*
 * HAMMER structural locking
 */

#include "hammer.h"

void
hammer_lock(struct hammer_lock *lock)
{
	thread_t td = curthread;

	crit_enter();
	++lock->refs;
	if (lock->locktd != td) {
		while (lock->locktd != NULL) {
			lock->wanted = 1;
			tsleep(lock, 0, "hmrlck", 0);
		}
		lock->locktd = td;
	}
	crit_exit();
}

void
hammer_unlock(struct hammer_lock *lock)
{
	KKASSERT(lock->locktd == curthread);
	KKASSERT(lock->refs > 0);
	crit_enter();
	if (--lock->refs == 0) {
		lock->locktd = NULL;
		if (lock->wanted) {
			lock->wanted = 0;
			wakeup(lock);
		}
	}
	crit_exit();
}

void
hammer_ref(struct hammer_lock *lock)
{
	crit_enter();
	++lock->refs;
	crit_exit();
}

void
hammer_unref(struct hammer_lock *lock)
{
	crit_enter();
	--lock->refs;
	crit_exit();
}

void
hammer_lock_to_ref(struct hammer_lock *lock)
{
	crit_enter();
	++lock->refs;
	hammer_unlock(lock);
	crit_exit();
}

void
hammer_ref_to_lock(struct hammer_lock *lock)
{
	crit_enter();
	hammer_lock(lock);
	--lock->refs;
	crit_exit();
}

u_int32_t
hammer_to_unix_xid(uuid_t *uuid)
{
	return(*(u_int32_t *)&uuid->node[2]);
}

void
hammer_to_timespec(u_int64_t hammerts, struct timespec *ts)
{
	ts->tv_sec = hammerts / 1000000000;
	ts->tv_nsec = hammerts % 1000000000;
}

/*
 * Convert a HAMMER filesystem object type to a vnode type
 */
enum vtype
hammer_get_vnode_type(u_int8_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(VDIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(VREG);
	case HAMMER_OBJTYPE_DBFILE:
		return(VDATABASE);
	case HAMMER_OBJTYPE_FIFO:
		return(VFIFO);
	case HAMMER_OBJTYPE_CDEV:
		return(VCHR);
	case HAMMER_OBJTYPE_BDEV:
		return(VBLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(VLNK);
	default:
		return(VBAD);
	}
	/* not reached */
}

u_int8_t
hammer_get_obj_type(enum vtype vtype)
{
	switch(vtype) {
	case VDIR:
		return(HAMMER_OBJTYPE_DIRECTORY);
	case VREG:
		return(HAMMER_OBJTYPE_REGFILE);
	case VDATABASE:
		return(HAMMER_OBJTYPE_DBFILE);
	case VFIFO:
		return(HAMMER_OBJTYPE_FIFO);
	case VCHR:
		return(HAMMER_OBJTYPE_CDEV);
	case VBLK:
		return(HAMMER_OBJTYPE_BDEV);
	case VLNK:
		return(HAMMER_OBJTYPE_SOFTLINK);
	default:
		return(HAMMER_OBJTYPE_UNKNOWN);
	}
	/* not reached */
}

/*
 * Return a namekey hash.   The 64 bit namekey hash consists of a 32 bit
 * crc in the MSB and 0 in the LSB.  The caller will use the low bits to
 * generate a unique key and will scan all entries with the same upper
 * 32 bits when issuing a lookup.
 */
int64_t
hammer_directory_namekey(void *name, int len)
{
	int64_t key;

	key = (int64_t)crc32(name, len) << 32;
	return(key);
}

