/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdSynch.c,v 1.21 2004/05/05 20:07:52 njl Exp $
 */

/*
 * 6.1 : Mutual Exclusion and Synchronisation
 */

#include "acpi.h"
#include "accommon.h"

#include "opt_acpi.h"

#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <dev/acpica/acpivar.h>

#define _COMPONENT	ACPI_OS_SERVICES
ACPI_MODULE_NAME("SYNCH")

MALLOC_DEFINE(M_ACPISEM, "acpisem", "ACPI semaphore");

#define AS_LOCK(as)		spin_lock(&(as)->as_spin)
#define AS_UNLOCK(as)		spin_unlock(&(as)->as_spin)
#define AS_LOCK_DECL

/*
 * Simple counting semaphore implemented using a mutex.  (Subsequently used
 * in the OSI code to implement a mutex.  Go figure.)
 */
struct acpi_semaphore {
    struct	spinlock as_spin;
    UINT32	as_units;
    UINT32	as_maxunits;
    UINT32	as_pendings;
    UINT32	as_resetting;
    UINT32	as_timeouts;
};

#ifndef ACPI_NO_SEMAPHORES
#ifndef ACPI_SEMAPHORES_MAX_PENDING
#define ACPI_SEMAPHORES_MAX_PENDING	4
#endif
static int	acpi_semaphore_debug = 0;
TUNABLE_INT("debug.acpi_semaphore_debug", &acpi_semaphore_debug);
SYSCTL_INT(_debug_acpi, OID_AUTO, semaphore_debug, CTLFLAG_RW,
	   &acpi_semaphore_debug, 0, "Enable ACPI semaphore debug messages");
#endif /* !ACPI_NO_SEMAPHORES */

ACPI_STATUS
AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits,
    ACPI_HANDLE *OutHandle)
{
#ifndef ACPI_NO_SEMAPHORES
    struct acpi_semaphore	*as;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (OutHandle == NULL)
	return_ACPI_STATUS (AE_BAD_PARAMETER);
    if (InitialUnits > MaxUnits)
	return_ACPI_STATUS (AE_BAD_PARAMETER);

    as = kmalloc(sizeof(*as), M_ACPISEM, M_INTWAIT | M_ZERO);

    spin_init(&as->as_spin, "AcpiOsSem");
    as->as_units = InitialUnits;
    as->as_maxunits = MaxUnits;
    as->as_pendings = as->as_resetting = as->as_timeouts = 0;

    ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	"created semaphore %p max %d, initial %d\n", 
	as, InitialUnits, MaxUnits));

    *OutHandle = (ACPI_HANDLE)as;
#else
    *OutHandle = (ACPI_HANDLE)OutHandle;
#endif /* !ACPI_NO_SEMAPHORES */

    return_ACPI_STATUS (AE_OK);
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_HANDLE Handle)
{
#ifndef ACPI_NO_SEMAPHORES
    struct acpi_semaphore *as = (struct acpi_semaphore *)Handle;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "destroyed semaphore %p\n", as));
    spin_uninit(&as->as_spin);
    kfree(as, M_ACPISEM);
#endif /* !ACPI_NO_SEMAPHORES */

    return_ACPI_STATUS (AE_OK);
}

ACPI_STATUS
AcpiOsWaitSemaphore(ACPI_HANDLE Handle, UINT32 Units, UINT16 Timeout)
{
#ifndef ACPI_NO_SEMAPHORES
    ACPI_STATUS			result;
    struct acpi_semaphore	*as = (struct acpi_semaphore *)Handle;
    int				rv, tmo;
    struct timeval		timeouttv, currenttv, timelefttv;
    AS_LOCK_DECL;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (as == NULL)
	return_ACPI_STATUS (AE_BAD_PARAMETER);

    if (cold)
	return_ACPI_STATUS (AE_OK);

#if 0
    if (as->as_units < Units && as->as_timeouts > 10) {
	kprintf("%s: semaphore %p too many timeouts, resetting\n", __func__, as);
	AS_LOCK(as);
	as->as_units = as->as_maxunits;
	if (as->as_pendings)
	    as->as_resetting = 1;
	as->as_timeouts = 0;
	wakeup(as);
	AS_UNLOCK(as);
	return_ACPI_STATUS (AE_TIME);
    }

    if (as->as_resetting)
	return_ACPI_STATUS (AE_TIME);
#endif

    /* a timeout of ACPI_WAIT_FOREVER means "forever" */
    if (Timeout == ACPI_WAIT_FOREVER) {
	tmo = 0;
	timeouttv.tv_sec = ((0xffff/1000) + 1);	/* cf. ACPI spec */
	timeouttv.tv_usec = 0;
    } else {
	/* compute timeout using microseconds per tick */
	tmo = (Timeout * 1000) / (1000000 / hz);
	if (tmo <= 0)
	    tmo = 1;
	timeouttv.tv_sec  = Timeout / 1000;
	timeouttv.tv_usec = (Timeout % 1000) * 1000;
    }

    /* calculate timeout value in timeval */
    getmicrouptime(&currenttv);
    timevaladd(&timeouttv, &currenttv);

    AS_LOCK(as);
    ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	"get %d units from semaphore %p (has %d), timeout %d\n",
	Units, as, as->as_units, Timeout));
    for (;;) {
	if (as->as_maxunits == ACPI_NO_UNIT_LIMIT) {
	    result = AE_OK;
	    break;
	}
	if (as->as_units >= Units) {
	    as->as_units -= Units;
	    result = AE_OK;
	    break;
	}

	/* limit number of pending treads */
	if (as->as_pendings >= ACPI_SEMAPHORES_MAX_PENDING) {
	    result = AE_TIME;
	    break;
	}

	/* if timeout values of zero is specified, return immediately */
	if (Timeout == 0) {
	    result = AE_TIME;
	    break;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	    "semaphore blocked, calling ssleep(%p, %p, %d, \"acsem\", %d)\n",
	    as, &as->as_spin, PCATCH, tmo));

	as->as_pendings++;

	if (acpi_semaphore_debug) {
	    kprintf("%s: Sleep %jd, pending %jd, semaphore %p, thread %jd\n",
		__func__, (intmax_t)Timeout,
		(intmax_t)as->as_pendings, as,
		(intmax_t)AcpiOsGetThreadId());
	}

	rv = ssleep(as, &as->as_spin, PCATCH, "acsem", tmo);

	as->as_pendings--;

#if 0
	if (as->as_resetting) {
	    /* semaphore reset, return immediately */
	    if (as->as_pendings == 0) {
		as->as_resetting = 0;
	    }
	    result = AE_TIME;
	    break;
	}
#endif

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "ssleep(%d) returned %d\n", tmo, rv));
	if (rv == EWOULDBLOCK) {
	    result = AE_TIME;
	    break;
	}

	/* check if we already awaited enough */
	timelefttv = timeouttv;
	getmicrouptime(&currenttv);
	timevalsub(&timelefttv, &currenttv);
	if (timelefttv.tv_sec < 0) {
	    ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "await semaphore %p timeout\n",
		as));
	    result = AE_TIME;
	    break;
	}

	/* adjust timeout for the next sleep */
	tmo = (timelefttv.tv_sec * 1000000 + timelefttv.tv_usec) /
	    (1000000 / hz);
	if (tmo <= 0)
	    tmo = 1;

	if (acpi_semaphore_debug) {
	    kprintf("%s: Wakeup timeleft(%ju, %ju), tmo %ju, sem %p, thread %jd\n",
		__func__,
		(intmax_t)timelefttv.tv_sec, (intmax_t)timelefttv.tv_usec,
		(intmax_t)tmo, as, (intmax_t)AcpiOsGetThreadId());
	}
    }

    if (acpi_semaphore_debug) {
	if (result == AE_TIME && Timeout > 0) {
	    kprintf("%s: Timeout %d, pending %d, semaphore %p\n",
		__func__, Timeout, as->as_pendings, as);
	}
	if (ACPI_SUCCESS(result) &&
	    (as->as_timeouts > 0 || as->as_pendings > 0))
	{
	    kprintf("%s: Acquire %d, units %d, pending %d, sem %p, thread %jd\n",
		__func__, Units, as->as_units, as->as_pendings, as,
		(intmax_t)AcpiOsGetThreadId());
	}
    }

    if (result == AE_TIME)
	as->as_timeouts++;
    else
	as->as_timeouts = 0;

    AS_UNLOCK(as);
    return_ACPI_STATUS (result);
#else
    return_ACPI_STATUS (AE_OK);
#endif /* !ACPI_NO_SEMAPHORES */
}

ACPI_STATUS
AcpiOsSignalSemaphore(ACPI_HANDLE Handle, UINT32 Units)
{
#ifndef ACPI_NO_SEMAPHORES
    struct acpi_semaphore	*as = (struct acpi_semaphore *)Handle;
    AS_LOCK_DECL;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (as == NULL)
	return_ACPI_STATUS(AE_BAD_PARAMETER);

    AS_LOCK(as);
    ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	"return %d units to semaphore %p (has %d)\n",
	Units, as, as->as_units));
    if (as->as_maxunits != ACPI_NO_UNIT_LIMIT) {
	as->as_units += Units;
	if (as->as_units > as->as_maxunits)
	    as->as_units = as->as_maxunits;
    }

    if (acpi_semaphore_debug && (as->as_timeouts > 0 || as->as_pendings > 0)) {
	kprintf("%s: Release %d, units %d, pending %d, semaphore %p, thread %jd\n",
	    __func__, Units, as->as_units, as->as_pendings, as,
	    (intmax_t)AcpiOsGetThreadId());
    }

    wakeup(as);
    AS_UNLOCK(as);
#endif /* !ACPI_NO_SEMAPHORES */

    return_ACPI_STATUS (AE_OK);
}

struct acpi_spinlock {
    struct spinlock lock;
#ifdef ACPI_DEBUG_LOCKS
    thread_t	owner;
    const char *func;
    int line;
#endif
};

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
    ACPI_SPINLOCK spin;

    if (OutHandle == NULL)
	return (AE_BAD_PARAMETER);
    spin = kmalloc(sizeof(*spin), M_ACPISEM, M_INTWAIT|M_ZERO);
    spin_init(&spin->lock, "AcpiOsLock");
#ifdef ACPI_DEBUG_LOCKS
    spin->owner = NULL;
    spin->func = "";
    spin->line = 0;
#endif
    *OutHandle = spin;
    return (AE_OK);
}

void
AcpiOsDeleteLock (ACPI_SPINLOCK Spin)
{
    if (Spin == NULL)
	return;
    spin_uninit(&Spin->lock);
    kfree(Spin, M_ACPISEM);
}

/*
 * OS-dependent locking primitives.  These routines should be able to be
 * called from an interrupt-handler or cpu_idle thread.
 *
 * NB: some of ACPI-CA functions with locking flags, say AcpiSetRegister(),
 * are changed to unconditionally call AcpiOsAcquireLock/AcpiOsReleaseLock.
 */
ACPI_CPU_FLAGS
#ifdef ACPI_DEBUG_LOCKS
_AcpiOsAcquireLock (ACPI_SPINLOCK Spin, const char *func, int line)
#else
AcpiOsAcquireLock (ACPI_SPINLOCK Spin)
#endif
{
    spin_lock(&Spin->lock);

#ifdef ACPI_DEBUG_LOCKS
    if (Spin->owner) {
	kprintf("%p(%s:%d): acpi_spinlock %p already held by %p(%s:%d)\n",
		curthread, func, line, Spin, Spin->owner, Spin->func,
		Spin->line);
	print_backtrace(-1);
    } else {
	Spin->owner = curthread;
	Spin->func = func;
	Spin->line = line;
    }
#endif
    return(0);
}

void
AcpiOsReleaseLock (ACPI_SPINLOCK Spin, ACPI_CPU_FLAGS Flags)
{
#ifdef ACPI_DEBUG_LOCKS
    if (Flags) {
	if (Spin->owner != NULL) {
	    kprintf("%p: acpi_spinlock %p is unexectedly held by %p(%s:%d)\n",
		    curthread, Spin, Spin->owner, Spin->func, Spin->line);
	    print_backtrace(-1);
	} else
	    return;
    }
    Spin->owner = NULL;
    Spin->func = "";
    Spin->line = 0;
#endif
    spin_unlock(&Spin->lock);
}

/* Section 5.2.9.1:  global lock acquire/release functions */
#define GL_ACQUIRED	(-1)
#define GL_BUSY		0
#define GL_BIT_PENDING	0x1
#define GL_BIT_OWNED	0x2
#define GL_BIT_MASK	(GL_BIT_PENDING | GL_BIT_OWNED)

/*
 * Acquire the global lock.  If busy, set the pending bit.  The caller
 * will wait for notification from the BIOS that the lock is available
 * and then attempt to acquire it again.
 */
int
acpi_acquire_global_lock(uint32_t *lock)
{
	uint32_t new, old;

	do {
		old = *lock;
		new = ((old & ~GL_BIT_MASK) | GL_BIT_OWNED) |
			((old >> 1) & GL_BIT_PENDING);
	} while (atomic_cmpset_int(lock, old, new) == 0);

	return ((new < GL_BIT_MASK) ? GL_ACQUIRED : GL_BUSY);
}

/*
 * Release the global lock, returning whether there is a waiter pending.
 * If the BIOS set the pending bit, OSPM must notify the BIOS when it
 * releases the lock.
 */
int
acpi_release_global_lock(uint32_t *lock)
{
	uint32_t new, old;

	do {
		old = *lock;
		new = old & ~GL_BIT_MASK;
	} while (atomic_cmpset_int(lock, old, new) == 0);

	return (old & GL_BIT_PENDING);
}
