/*
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/ndis/subr_ntoskrnl.c,v 1.40 2004/07/20 20:28:57 wpaul Exp $
 * $DragonFly: src/sys/emulation/ndis/subr_ntoskrnl.c,v 1.13 2006/12/23 00:27:02 swildner Exp $
 */

#include <sys/ctype.h>
#include <sys/unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>

#include <sys/callout.h>
#if __FreeBSD_version > 502113
#include <sys/kdb.h>
#endif
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/mplock2.h>

#include <machine/atomic.h>
#include <machine/clock.h>
#include <machine/stdarg.h>

#include "regcall.h"
#include "pe_var.h"
#include "resource_var.h"
#include "ntoskrnl_var.h"
#include "ndis_var.h"
#include "hal_var.h"

#define __regparm __attribute__((regparm(3)))

#define FUNC void(*)(void)

__stdcall static uint8_t ntoskrnl_unicode_equal(ndis_unicode_string *,
	ndis_unicode_string *, uint8_t);
__stdcall static void ntoskrnl_unicode_copy(ndis_unicode_string *,
	ndis_unicode_string *);
__stdcall static ndis_status ntoskrnl_unicode_to_ansi(ndis_ansi_string *,
	ndis_unicode_string *, uint8_t);
__stdcall static ndis_status ntoskrnl_ansi_to_unicode(ndis_unicode_string *,
	ndis_ansi_string *, uint8_t);
__stdcall static void *ntoskrnl_iobuildsynchfsdreq(uint32_t, void *,
	void *, uint32_t, uint32_t *, void *, void *);

/*
 * registerized calls
 */
__stdcall __regcall static uint32_t
    ntoskrnl_iofcalldriver(REGARGS2(void *dobj, void *irp));
__stdcall __regcall static void
    ntoskrnl_iofcompletereq(REGARGS2(void *irp, uint8_t prioboost));
__stdcall __regcall static slist_entry *
    ntoskrnl_push_slist(REGARGS2(slist_header *head, slist_entry *entry));
__stdcall __regcall static slist_entry *
    ntoskrnl_pop_slist(REGARGS1(slist_header *head));
__stdcall __regcall static slist_entry *
    ntoskrnl_push_slist_ex(REGARGS2(slist_header *head, slist_entry *entry), kspin_lock *lock);
__stdcall __regcall static slist_entry *
    ntoskrnl_pop_slist_ex(REGARGS2(slist_header *head, kspin_lock *lock));

__stdcall __regcall static uint32_t
    ntoskrnl_interlock_inc(REGARGS1(volatile uint32_t *addend));
__stdcall __regcall static uint32_t
    ntoskrnl_interlock_dec(REGARGS1(volatile uint32_t *addend));
__stdcall __regcall static void
    ntoskrnl_interlock_addstat(REGARGS2(uint64_t *addend, uint32_t inc));
__stdcall __regcall static void
    ntoskrnl_objderef(REGARGS1(void *object));

__stdcall static uint32_t ntoskrnl_waitforobjs(uint32_t,
	nt_dispatch_header **, uint32_t, uint32_t, uint32_t, uint8_t,
	int64_t *, wait_block *);
static void ntoskrnl_wakeup(void *);
static void ntoskrnl_timercall(void *);
static void ntoskrnl_run_dpc(void *);
__stdcall static void ntoskrnl_writereg_ushort(uint16_t *, uint16_t);
__stdcall static uint16_t ntoskrnl_readreg_ushort(uint16_t *);
__stdcall static void ntoskrnl_writereg_ulong(uint32_t *, uint32_t);
__stdcall static uint32_t ntoskrnl_readreg_ulong(uint32_t *);
__stdcall static void ntoskrnl_writereg_uchar(uint8_t *, uint8_t);
__stdcall static uint8_t ntoskrnl_readreg_uchar(uint8_t *);
__stdcall static int64_t _allmul(int64_t, int64_t);
__stdcall static int64_t _alldiv(int64_t, int64_t);
__stdcall static int64_t _allrem(int64_t, int64_t);
__regparm static int64_t _allshr(int64_t, uint8_t);
__regparm static int64_t _allshl(int64_t, uint8_t);
__stdcall static uint64_t _aullmul(uint64_t, uint64_t);
__stdcall static uint64_t _aulldiv(uint64_t, uint64_t);
__stdcall static uint64_t _aullrem(uint64_t, uint64_t);
__regparm static uint64_t _aullshr(uint64_t, uint8_t);
__regparm static uint64_t _aullshl(uint64_t, uint8_t);
__stdcall static void *ntoskrnl_allocfunc(uint32_t, size_t, uint32_t);
__stdcall static void ntoskrnl_freefunc(void *);
static slist_entry *ntoskrnl_pushsl(slist_header *, slist_entry *);
static slist_entry *ntoskrnl_popsl(slist_header *);
__stdcall static void ntoskrnl_init_lookaside(paged_lookaside_list *,
	lookaside_alloc_func *, lookaside_free_func *,
	uint32_t, size_t, uint32_t, uint16_t);
__stdcall static void ntoskrnl_delete_lookaside(paged_lookaside_list *);
__stdcall static void ntoskrnl_init_nplookaside(npaged_lookaside_list *,
	lookaside_alloc_func *, lookaside_free_func *,
	uint32_t, size_t, uint32_t, uint16_t);
__stdcall static void ntoskrnl_delete_nplookaside(npaged_lookaside_list *);
__stdcall static void ntoskrnl_freemdl(ndis_buffer *);
__stdcall static uint32_t ntoskrnl_sizeofmdl(void *, size_t);
__stdcall static void ntoskrnl_build_npaged_mdl(ndis_buffer *);
__stdcall static void *ntoskrnl_mmaplockedpages(ndis_buffer *, uint8_t);
__stdcall static void *ntoskrnl_mmaplockedpages_cache(ndis_buffer *,
	uint8_t, uint32_t, void *, uint32_t, uint32_t);
__stdcall static void ntoskrnl_munmaplockedpages(void *, ndis_buffer *);
__stdcall static void ntoskrnl_init_lock(kspin_lock *);
__stdcall static size_t ntoskrnl_memcmp(const void *, const void *, size_t);
__stdcall static void ntoskrnl_init_ansi_string(ndis_ansi_string *, char *);
__stdcall static void ntoskrnl_init_unicode_string(ndis_unicode_string *,
	uint16_t *);
__stdcall static void ntoskrnl_free_unicode_string(ndis_unicode_string *);
__stdcall static void ntoskrnl_free_ansi_string(ndis_ansi_string *);
__stdcall static ndis_status ntoskrnl_unicode_to_int(ndis_unicode_string *,
	uint32_t, uint32_t *);
static int atoi (const char *);
static long atol (const char *);
static int rand(void);
static void ntoskrnl_time(uint64_t *);
__stdcall static uint8_t ntoskrnl_wdmver(uint8_t, uint8_t);
static void ntoskrnl_thrfunc(void *);
__stdcall static ndis_status ntoskrnl_create_thread(ndis_handle *,
	uint32_t, void *, ndis_handle, void *, void *, void *);
__stdcall static ndis_status ntoskrnl_thread_exit(ndis_status);
__stdcall static ndis_status ntoskrnl_devprop(device_object *, uint32_t,
	uint32_t, void *, uint32_t *);
__stdcall static void ntoskrnl_init_mutex(kmutant *, uint32_t);
__stdcall static uint32_t ntoskrnl_release_mutex(kmutant *, uint8_t);
__stdcall static uint32_t ntoskrnl_read_mutex(kmutant *);
__stdcall static ndis_status ntoskrnl_objref(ndis_handle, uint32_t, void *,
    uint8_t, void **, void **);
__stdcall static uint32_t ntoskrnl_zwclose(ndis_handle);
static uint32_t ntoskrnl_dbgprint(char *, ...);
__stdcall static void ntoskrnl_debugger(void);
__stdcall static void dummy(void);

static struct lwkt_token ntoskrnl_dispatchtoken;
static kspin_lock ntoskrnl_global;
static int ntoskrnl_kth = 0;
static struct nt_objref_head ntoskrnl_reflist;

static MALLOC_DEFINE(M_NDIS, "ndis", "ndis emulation");

int
ntoskrnl_libinit(void)
{
	lwkt_token_init(&ntoskrnl_dispatchtoken, "ndiskrnl");
	ntoskrnl_init_lock(&ntoskrnl_global);
	TAILQ_INIT(&ntoskrnl_reflist);
	return(0);
}

int
ntoskrnl_libfini(void)
{
	lwkt_token_uninit(&ntoskrnl_dispatchtoken);
	return(0);
}

__stdcall static uint8_t 
ntoskrnl_unicode_equal(ndis_unicode_string *str1,
		       ndis_unicode_string *str2,
		       uint8_t caseinsensitive)
{
	int			i;

	if (str1->nus_len != str2->nus_len)
		return(FALSE);

	for (i = 0; i < str1->nus_len; i++) {
		if (caseinsensitive == TRUE) {
			if (toupper((char)(str1->nus_buf[i] & 0xFF)) !=
			    toupper((char)(str2->nus_buf[i] & 0xFF)))
				return(FALSE);
		} else {
			if (str1->nus_buf[i] != str2->nus_buf[i])
				return(FALSE);
		}
	}

	return(TRUE);
}

__stdcall static void
ntoskrnl_unicode_copy(ndis_unicode_string *dest,
		      ndis_unicode_string *src)
{

	if (dest->nus_maxlen >= src->nus_len)
		dest->nus_len = src->nus_len;
	else
		dest->nus_len = dest->nus_maxlen;
	memcpy(dest->nus_buf, src->nus_buf, dest->nus_len);
	return;
}

__stdcall static ndis_status
ntoskrnl_unicode_to_ansi(ndis_ansi_string *dest,
			 ndis_unicode_string *src,
			 uint8_t allocate)
{
	char			*astr = NULL;

	if (dest == NULL || src == NULL)
		return(NDIS_STATUS_FAILURE);

	if (allocate == TRUE) {
		if (ndis_unicode_to_ascii(src->nus_buf, src->nus_len, &astr))
			return(NDIS_STATUS_FAILURE);
		dest->nas_buf = astr;
		dest->nas_len = dest->nas_maxlen = strlen(astr);
	} else {
		dest->nas_len = src->nus_len / 2; /* XXX */
		if (dest->nas_maxlen < dest->nas_len)
			dest->nas_len = dest->nas_maxlen;
		ndis_unicode_to_ascii(src->nus_buf, dest->nas_len * 2,
		    &dest->nas_buf);
	}
	return (NDIS_STATUS_SUCCESS);
}

__stdcall static ndis_status
ntoskrnl_ansi_to_unicode(ndis_unicode_string *dest,
			 ndis_ansi_string *src,
			 uint8_t allocate)
{
	uint16_t		*ustr = NULL;

	if (dest == NULL || src == NULL)
		return(NDIS_STATUS_FAILURE);

	if (allocate == TRUE) {
		if (ndis_ascii_to_unicode(src->nas_buf, &ustr))
			return(NDIS_STATUS_FAILURE);
		dest->nus_buf = ustr;
		dest->nus_len = dest->nus_maxlen = strlen(src->nas_buf) * 2;
	} else {
		dest->nus_len = src->nas_len * 2; /* XXX */
		if (dest->nus_maxlen < dest->nus_len)
			dest->nus_len = dest->nus_maxlen;
		ndis_ascii_to_unicode(src->nas_buf, &dest->nus_buf);
	}
	return (NDIS_STATUS_SUCCESS);
}

__stdcall static void *
ntoskrnl_iobuildsynchfsdreq(uint32_t func, void *dobj, void *buf,
			    uint32_t len, uint32_t *off,
			    void *event, void *status)
{
	return(NULL);
}
	
__stdcall __regcall static uint32_t
ntoskrnl_iofcalldriver(REGARGS2(void *dobj, void *irp))
{
	return(0);
}

__stdcall __regcall static void
ntoskrnl_iofcompletereq(REGARGS2(void *irp, uint8_t prioboost))
{
}

static void
ntoskrnl_wakeup(void *arg)
{
	nt_dispatch_header	*obj;
	wait_block		*w;
	list_entry		*e;
	struct thread		*td;

	obj = arg;

	lwkt_gettoken(&ntoskrnl_dispatchtoken);
	obj->dh_sigstate = TRUE;
	e = obj->dh_waitlisthead.nle_flink;
	while (e != &obj->dh_waitlisthead) {
		w = (wait_block *)e;
		td = w->wb_kthread;
		ndis_thresume(td);
		/*
		 * For synchronization objects, only wake up
		 * the first waiter.
		 */
		if (obj->dh_type == EVENT_TYPE_SYNC)
			break;
		e = e->nle_flink;
	}
	lwkt_reltoken(&ntoskrnl_dispatchtoken);
}

static void 
ntoskrnl_time(uint64_t *tval)
{
	struct timespec		ts;

	nanotime(&ts);
	*tval = (uint64_t)ts.tv_nsec / 100 + (uint64_t)ts.tv_sec * 10000000 +
	    11644473600LL;

	return;
}

/*
 * KeWaitForSingleObject() is a tricky beast, because it can be used
 * with several different object types: semaphores, timers, events,
 * mutexes and threads. Semaphores don't appear very often, but the
 * other object types are quite common. KeWaitForSingleObject() is
 * what's normally used to acquire a mutex, and it can be used to
 * wait for a thread termination.
 *
 * The Windows NDIS API is implemented in terms of Windows kernel
 * primitives, and some of the object manipulation is duplicated in
 * NDIS. For example, NDIS has timers and events, which are actually
 * Windows kevents and ktimers. Now, you're supposed to only use the
 * NDIS variants of these objects within the confines of the NDIS API,
 * but there are some naughty developers out there who will use
 * KeWaitForSingleObject() on NDIS timer and event objects, so we
 * have to support that as well. Conseqently, our NDIS timer and event
 * code has to be closely tied into our ntoskrnl timer and event code,
 * just as it is in Windows.
 *
 * KeWaitForSingleObject() may do different things for different kinds
 * of objects:
 *
 * - For events, we check if the event has been signalled. If the
 *   event is already in the signalled state, we just return immediately,
 *   otherwise we wait for it to be set to the signalled state by someone
 *   else calling KeSetEvent(). Events can be either synchronization or
 *   notification events.
 *
 * - For timers, if the timer has already fired and the timer is in
 *   the signalled state, we just return, otherwise we wait on the
 *   timer. Unlike an event, timers get signalled automatically when
 *   they expire rather than someone having to trip them manually.
 *   Timers initialized with KeInitializeTimer() are always notification
 *   events: KeInitializeTimerEx() lets you initialize a timer as
 *   either a notification or synchronization event.
 *
 * - For mutexes, we try to acquire the mutex and if we can't, we wait
 *   on the mutex until it's available and then grab it. When a mutex is
 *   released, it enters the signaled state, which wakes up one of the
 *   threads waiting to acquire it. Mutexes are always synchronization
 *   events.
 *
 * - For threads, the only thing we do is wait until the thread object
 *   enters a signalled state, which occurs when the thread terminates.
 *   Threads are always notification events.
 *
 * A notification event wakes up all threads waiting on an object. A
 * synchronization event wakes up just one. Also, a synchronization event
 * is auto-clearing, which means we automatically set the event back to
 * the non-signalled state once the wakeup is done.
 */

__stdcall uint32_t
ntoskrnl_waitforobj(nt_dispatch_header *obj, uint32_t reason,
		    uint32_t mode, uint8_t alertable, int64_t *duetime)
{
	struct thread		*td = curthread;
	kmutant			*km;
	wait_block		w;
	struct timeval		tv;
	int			error = 0;
	int			ticks;
	uint64_t		curtime;

	if (obj == NULL)
		return(STATUS_INVALID_PARAMETER);

	lwkt_gettoken(&ntoskrnl_dispatchtoken);

	/*
	 * See if the object is a mutex. If so, and we already own
	 * it, then just increment the acquisition count and return.
         *
         * For any other kind of object, see if it's already in the
	 * signalled state, and if it is, just return. If the object
         * is marked as a synchronization event, reset the state to
         * unsignalled.
	 */

	if (obj->dh_size == OTYPE_MUTEX) {
		km = (kmutant *)obj;
		if (km->km_ownerthread == NULL ||
		    km->km_ownerthread == curthread->td_proc) {
			obj->dh_sigstate = FALSE;
			km->km_acquirecnt++;
			km->km_ownerthread = curthread->td_proc;
			lwkt_reltoken(&ntoskrnl_dispatchtoken);
			return (STATUS_SUCCESS);
		}
	} else if (obj->dh_sigstate == TRUE) {
		if (obj->dh_type == EVENT_TYPE_SYNC)
			obj->dh_sigstate = FALSE;
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		return (STATUS_SUCCESS);
	}

	w.wb_object = obj;
	w.wb_kthread = td;

	INSERT_LIST_TAIL((&obj->dh_waitlisthead), (&w.wb_waitlist));

	/*
	 * The timeout value is specified in 100 nanosecond units
	 * and can be a positive or negative number. If it's positive,
	 * then the duetime is absolute, and we need to convert it
	 * to an absolute offset relative to now in order to use it.
	 * If it's negative, then the duetime is relative and we
	 * just have to convert the units.
	 */

	if (duetime != NULL) {
		if (*duetime < 0) {
			tv.tv_sec = - (*duetime) / 10000000;
			tv.tv_usec = (- (*duetime) / 10) -
			    (tv.tv_sec * 1000000);
		} else {
			ntoskrnl_time(&curtime);
			if (*duetime < curtime)
				tv.tv_sec = tv.tv_usec = 0;
			else {
				tv.tv_sec = ((*duetime) - curtime) / 10000000;
				tv.tv_usec = ((*duetime) - curtime) / 10 -
				    (tv.tv_sec * 1000000);
			}
		}
	}

	lwkt_reltoken(&ntoskrnl_dispatchtoken);

	ticks = 1 + tv.tv_sec * hz + tv.tv_usec * hz / 1000000;
	error = ndis_thsuspend(td, duetime == NULL ? 0 : ticks);

	lwkt_gettoken(&tokref, &ntoskrnl_dispatchtoken);

	/* We timed out. Leave the object alone and return status. */

	if (error == EWOULDBLOCK) {
		REMOVE_LIST_ENTRY((&w.wb_waitlist));
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		return(STATUS_TIMEOUT);
	}

	/*
	 * Mutexes are always synchronization objects, which means
         * if several threads are waiting to acquire it, only one will
         * be woken up. If that one is us, and the mutex is up for grabs,
         * grab it.
	 */

	if (obj->dh_size == OTYPE_MUTEX) {
		km = (kmutant *)obj;
		if (km->km_ownerthread == NULL) {
			km->km_ownerthread = curthread->td_proc;
			km->km_acquirecnt++;
		}
	}

	if (obj->dh_type == EVENT_TYPE_SYNC)
		obj->dh_sigstate = FALSE;
	REMOVE_LIST_ENTRY((&w.wb_waitlist));

	lwkt_reltoken(&ntoskrnl_dispatchtoken);

	return(STATUS_SUCCESS);
}

__stdcall static uint32_t
ntoskrnl_waitforobjs(uint32_t cnt, nt_dispatch_header *obj[],
		     uint32_t wtype, uint32_t reason, uint32_t mode,
		     uint8_t alertable, int64_t *duetime,
		     wait_block *wb_array)
{
	struct thread		*td = curthread;
	kmutant			*km;
	wait_block		_wb_array[THREAD_WAIT_OBJECTS];
	wait_block		*w;
	struct timeval		tv;
	int			i, wcnt = 0, widx = 0, error = 0;
	uint64_t		curtime;
	struct timespec		t1, t2;

	if (cnt > MAX_WAIT_OBJECTS)
		return(STATUS_INVALID_PARAMETER);
	if (cnt > THREAD_WAIT_OBJECTS && wb_array == NULL)
		return(STATUS_INVALID_PARAMETER);

	lwkt_gettoken(&ntoskrnl_dispatchtoken);

	if (wb_array == NULL)
		w = &_wb_array[0];
	else
		w = wb_array;

	tv.tv_sec = 0;		/* fix compiler warning */
	tv.tv_usec = 0;		/* fix compiler warning */

	/* First pass: see if we can satisfy any waits immediately. */

	for (i = 0; i < cnt; i++) {
		if (obj[i]->dh_size == OTYPE_MUTEX) {
			km = (kmutant *)obj[i];
			if (km->km_ownerthread == NULL ||
			    km->km_ownerthread == curthread->td_proc) {
				obj[i]->dh_sigstate = FALSE;
				km->km_acquirecnt++;
				km->km_ownerthread = curthread->td_proc;
				if (wtype == WAITTYPE_ANY) {
					lwkt_reltoken(&ntoskrnl_dispatchtoken);
					return (STATUS_WAIT_0 + i);
				}
			}
		} else if (obj[i]->dh_sigstate == TRUE) {
			if (obj[i]->dh_type == EVENT_TYPE_SYNC)
				obj[i]->dh_sigstate = FALSE;
			if (wtype == WAITTYPE_ANY) {
				lwkt_reltoken(&ntoskrnl_dispatchtoken);
				return (STATUS_WAIT_0 + i);
			}
		}
	}

	/*
	 * Second pass: set up wait for anything we can't
	 * satisfy immediately.
	 */

	for (i = 0; i < cnt; i++) {
		if (obj[i]->dh_sigstate == TRUE)
			continue;
		INSERT_LIST_TAIL((&obj[i]->dh_waitlisthead),
		    (&w[i].wb_waitlist));
		w[i].wb_kthread = td;
		w[i].wb_object = obj[i];
		wcnt++;
	}

	if (duetime) {
		if (*duetime < 0) {
			tv.tv_sec = -*duetime / 10000000;
			tv.tv_usec = (-*duetime / 10) - (tv.tv_sec * 1000000);
		} else {
			ntoskrnl_time(&curtime);
			if (*duetime < curtime) {
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			} else {
				tv.tv_sec = ((*duetime) - curtime) / 10000000;
				tv.tv_usec = ((*duetime) - curtime) / 10 -
				    (tv.tv_sec * 1000000);
			}
		}
	}

	while (wcnt) {
		nanotime(&t1);
		lwkt_reltoken(&ntoskrnl_dispatchtoken);

		if (duetime) {
			ticks = 1 + tv.tv_sec * hz + tv.tv_usec * hz / 1000000;
			error = ndis_thsuspend(td, ticks);
		} else {
			error = ndis_thsuspend(td, 0);
		}

		lwkt_gettoken(&ntoskrnl_dispatchtoken);
		nanotime(&t2);

		for (i = 0; i < cnt; i++) {
			if (obj[i]->dh_size == OTYPE_MUTEX) {
				km = (kmutant *)obj;
				if (km->km_ownerthread == NULL) {
					km->km_ownerthread =
					    curthread->td_proc;
					km->km_acquirecnt++;
				}
			}
			if (obj[i]->dh_sigstate == TRUE) {
				widx = i;
				if (obj[i]->dh_type == EVENT_TYPE_SYNC)
					obj[i]->dh_sigstate = FALSE;
				REMOVE_LIST_ENTRY((&w[i].wb_waitlist));
				wcnt--;
			}
		}

		if (error || wtype == WAITTYPE_ANY)
			break;

		if (duetime) {
			tv.tv_sec -= (t2.tv_sec - t1.tv_sec);
			tv.tv_usec -= (t2.tv_nsec - t1.tv_nsec) / 1000;
		}
	}

	if (wcnt) {
		for (i = 0; i < cnt; i++)
			REMOVE_LIST_ENTRY((&w[i].wb_waitlist));
	}

	if (error == EWOULDBLOCK) {
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		return(STATUS_TIMEOUT);
	}

	if (wtype == WAITTYPE_ANY && wcnt) {
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		return(STATUS_WAIT_0 + widx);
	}

	lwkt_reltoken(&ntoskrnl_dispatchtoken);

	return(STATUS_SUCCESS);
}

__stdcall static void
ntoskrnl_writereg_ushort(uint16_t *reg, uint16_t val)
{
	bus_space_write_2(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
	return;
}

__stdcall static uint16_t
ntoskrnl_readreg_ushort(uint16_t *reg)
{
	return(bus_space_read_2(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

__stdcall static void
ntoskrnl_writereg_ulong(uint32_t *reg, uint32_t val)
{
	bus_space_write_4(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
	return;
}

__stdcall static uint32_t
ntoskrnl_readreg_ulong(uint32_t *reg)
{
	return(bus_space_read_4(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

__stdcall static uint8_t
ntoskrnl_readreg_uchar(uint8_t *reg)
{
	return(bus_space_read_1(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg));
}

__stdcall static void
ntoskrnl_writereg_uchar(uint8_t *reg, uint8_t val)
{
	bus_space_write_1(NDIS_BUS_SPACE_MEM, 0x0, (bus_size_t)reg, val);
	return;
}

__stdcall static int64_t
_allmul(int64_t a, int64_t b)
{
	return (a * b);
}

__stdcall static int64_t
_alldiv(int64_t a, int64_t b)
{
	return (a / b);
}

__stdcall static int64_t
_allrem(int64_t a, int64_t b)
{
	return (a % b);
}

__stdcall static uint64_t
_aullmul(uint64_t a, uint64_t b)
{
	return (a * b);
}

__stdcall static uint64_t
_aulldiv(uint64_t a, uint64_t b)
{
	return (a / b);
}

__stdcall static uint64_t
_aullrem(uint64_t a, uint64_t b)
{
	return (a % b);
}

__regparm static int64_t
_allshl(int64_t a, uint8_t b)
{
	return (a << b);
}

__regparm static uint64_t
_aullshl(uint64_t a, uint8_t b)
{
	return (a << b);
}

__regparm static int64_t
_allshr(int64_t a, uint8_t b)
{
	return (a >> b);
}

__regparm static uint64_t
_aullshr(uint64_t a, uint8_t b)
{
	return (a >> b);
}

static slist_entry *
ntoskrnl_pushsl(slist_header *head, slist_entry *entry)
{
	slist_entry		*oldhead;

	oldhead = head->slh_list.slh_next;
	entry->sl_next = head->slh_list.slh_next;
	head->slh_list.slh_next = entry;
	head->slh_list.slh_depth++;
	head->slh_list.slh_seq++;

	return(oldhead);
}

static slist_entry *
ntoskrnl_popsl(slist_header *head)
{
	slist_entry		*first;

	first = head->slh_list.slh_next;
	if (first != NULL) {
		head->slh_list.slh_next = first->sl_next;
		head->slh_list.slh_depth--;
		head->slh_list.slh_seq++;
	}

	return(first);
}

__stdcall static void *
ntoskrnl_allocfunc(uint32_t pooltype, size_t size, uint32_t tag)
{
	return(kmalloc(size, M_DEVBUF, M_WAITOK));
}

__stdcall static void
ntoskrnl_freefunc(void *buf)
{
	kfree(buf, M_DEVBUF);
	return;
}

__stdcall static void
ntoskrnl_init_lookaside(paged_lookaside_list *lookaside,
			lookaside_alloc_func *allocfunc,
			lookaside_free_func *freefunc,
			uint32_t flags, size_t size,
			uint32_t tag, uint16_t depth)
{
	bzero((char *)lookaside, sizeof(paged_lookaside_list));

	if (size < sizeof(slist_entry))
		lookaside->nll_l.gl_size = sizeof(slist_entry);
	else
		lookaside->nll_l.gl_size = size;
	lookaside->nll_l.gl_tag = tag;
	if (allocfunc == NULL)
		lookaside->nll_l.gl_allocfunc = ntoskrnl_allocfunc;
	else
		lookaside->nll_l.gl_allocfunc = allocfunc;

	if (freefunc == NULL)
		lookaside->nll_l.gl_freefunc = ntoskrnl_freefunc;
	else
		lookaside->nll_l.gl_freefunc = freefunc;

	ntoskrnl_init_lock(&lookaside->nll_obsoletelock);

	lookaside->nll_l.gl_depth = LOOKASIDE_DEPTH;
	lookaside->nll_l.gl_maxdepth = LOOKASIDE_DEPTH;

	return;
}

__stdcall static void
ntoskrnl_delete_lookaside(paged_lookaside_list *lookaside)
{
	void			*buf;
	__stdcall void		(*freefunc)(void *);

	freefunc = lookaside->nll_l.gl_freefunc;
	while((buf = ntoskrnl_popsl(&lookaside->nll_l.gl_listhead)) != NULL)
		freefunc(buf);

	return;
}

__stdcall static void
ntoskrnl_init_nplookaside(npaged_lookaside_list *lookaside,
			  lookaside_alloc_func *allocfunc,
			  lookaside_free_func *freefunc,
			  uint32_t flags, size_t size,
			  uint32_t tag, uint16_t depth)
{
	bzero((char *)lookaside, sizeof(npaged_lookaside_list));

	if (size < sizeof(slist_entry))
		lookaside->nll_l.gl_size = sizeof(slist_entry);
	else
		lookaside->nll_l.gl_size = size;
	lookaside->nll_l.gl_tag = tag;
	if (allocfunc == NULL)
		lookaside->nll_l.gl_allocfunc = ntoskrnl_allocfunc;
	else
		lookaside->nll_l.gl_allocfunc = allocfunc;

	if (freefunc == NULL)
		lookaside->nll_l.gl_freefunc = ntoskrnl_freefunc;
	else
		lookaside->nll_l.gl_freefunc = freefunc;

	ntoskrnl_init_lock(&lookaside->nll_obsoletelock);

	lookaside->nll_l.gl_depth = LOOKASIDE_DEPTH;
	lookaside->nll_l.gl_maxdepth = LOOKASIDE_DEPTH;

	return;
}

__stdcall static void
ntoskrnl_delete_nplookaside(npaged_lookaside_list *lookaside)
{
	void			*buf;
	__stdcall void		(*freefunc)(void *);

	freefunc = lookaside->nll_l.gl_freefunc;
	while((buf = ntoskrnl_popsl(&lookaside->nll_l.gl_listhead)) != NULL)
		freefunc(buf);

	return;
}

/*
 * Note: the interlocked slist push and pop routines are
 * declared to be _fastcall in Windows. gcc 3.4 is supposed
 * to have support for this calling convention, however we
 * don't have that version available yet, so we kludge things
 * up using some inline assembly.
 */

__stdcall __regcall static slist_entry *
ntoskrnl_push_slist(REGARGS2(slist_header *head, slist_entry *entry))
{
	slist_entry		*oldhead;

	oldhead = (slist_entry *)FASTCALL3(ntoskrnl_push_slist_ex,
	    head, entry, &ntoskrnl_global);

	return(oldhead);
}

__stdcall __regcall static slist_entry *
ntoskrnl_pop_slist(REGARGS1(slist_header *head))
{
	slist_entry		*first;

	first = (slist_entry *)FASTCALL2(ntoskrnl_pop_slist_ex,
	    head, &ntoskrnl_global);

	return(first);
}

__stdcall __regcall static slist_entry *
ntoskrnl_push_slist_ex(REGARGS2(slist_header *head, slist_entry *entry), kspin_lock *lock)
{
	slist_entry		*oldhead;
	uint8_t			irql;

	irql = FASTCALL2(hal_lock, lock, DISPATCH_LEVEL);
	oldhead = ntoskrnl_pushsl(head, entry);
	FASTCALL2(hal_unlock, lock, irql);

	return(oldhead);
}

__stdcall __regcall static slist_entry *
ntoskrnl_pop_slist_ex(REGARGS2(slist_header *head, kspin_lock *lock))
{
	slist_entry		*first;
	uint8_t			irql;

	irql = FASTCALL2(hal_lock, lock, DISPATCH_LEVEL);
	first = ntoskrnl_popsl(head);
	FASTCALL2(hal_unlock, lock, irql);

	return(first);
}

__stdcall __regcall void
ntoskrnl_lock_dpc(REGARGS1(kspin_lock *lock))
{
	while (atomic_poll_acquire_int((volatile u_int *)lock) == 0)
		/* sit and spin */;
}

__stdcall __regcall void
ntoskrnl_unlock_dpc(REGARGS1(kspin_lock *lock))
{
	atomic_poll_release_int((volatile u_int *)lock);
}

__stdcall __regcall static uint32_t
ntoskrnl_interlock_inc(REGARGS1(volatile uint32_t *addend))
{
	atomic_add_long((volatile u_long *)addend, 1);
	return(*addend);
}

__stdcall __regcall static uint32_t
ntoskrnl_interlock_dec(REGARGS1(volatile uint32_t *addend))
{
	atomic_subtract_long((volatile u_long *)addend, 1);
	return(*addend);
}

__stdcall __regcall static void
ntoskrnl_interlock_addstat(REGARGS2(uint64_t *addend, uint32_t inc))
{
	uint8_t			irql;

	irql = FASTCALL2(hal_lock, &ntoskrnl_global, DISPATCH_LEVEL);
	*addend += inc;
	FASTCALL2(hal_unlock, &ntoskrnl_global, irql);

	return;
};

__stdcall static void
ntoskrnl_freemdl(ndis_buffer *mdl)
{
	ndis_buffer		*head;

	if (mdl == NULL || mdl->nb_process == NULL)
		return;

        head = mdl->nb_process;

        if (head->nb_flags != 0x1)
                return;

        mdl->nb_next = head->nb_next;
        head->nb_next = mdl;

	/* Decrement count of busy buffers. */

	head->nb_bytecount--;

	/*
	 * If the pool has been marked for deletion and there are
	 * no more buffers outstanding, nuke the pool.
	 */

	if (head->nb_byteoffset && head->nb_bytecount == 0)
		kfree(head, M_DEVBUF);

        return;
}

__stdcall static uint32_t
ntoskrnl_sizeofmdl(void *vaddr, size_t len)
{
	uint32_t		l;

        l = sizeof(struct ndis_buffer) +
	    (sizeof(uint32_t) * SPAN_PAGES(vaddr, len));

	return(l);
}

__stdcall static void
ntoskrnl_build_npaged_mdl(ndis_buffer *mdl)
{
	mdl->nb_mappedsystemva = (char *)mdl->nb_startva + mdl->nb_byteoffset;
	return;
}

__stdcall static void *
ntoskrnl_mmaplockedpages(ndis_buffer *buf, uint8_t accessmode)
{
	return(MDL_VA(buf));
}

__stdcall static void *
ntoskrnl_mmaplockedpages_cache(ndis_buffer *buf, uint8_t accessmode,
			       uint32_t cachetype, void *vaddr,
			       uint32_t bugcheck, uint32_t prio)
{
	return(MDL_VA(buf));
}

__stdcall static void
ntoskrnl_munmaplockedpages(void *vaddr, ndis_buffer *buf)
{
	return;
}

/*
 * The KeInitializeSpinLock(), KefAcquireSpinLockAtDpcLevel()
 * and KefReleaseSpinLockFromDpcLevel() appear to be analagous
 * to crit_enter()/crit_exit() in their use. We can't create a new mutex
 * lock here because there is no complimentary KeFreeSpinLock()
 * function. Instead, we grab a mutex from the mutex pool.
 */
__stdcall static void
ntoskrnl_init_lock(kspin_lock *lock)
{
	*lock = 0;

	return;
}

__stdcall static size_t
ntoskrnl_memcmp(const void *s1, const void *s2, size_t len)
{
	size_t			i, total = 0;
	uint8_t			*m1, *m2;

	m1 = __DECONST(char *, s1);
	m2 = __DECONST(char *, s2);

	for (i = 0; i < len; i++) {
		if (m1[i] == m2[i])
			total++;
	}
	return(total);
}

__stdcall static void
ntoskrnl_init_ansi_string(ndis_ansi_string *dst, char *src)
{
	ndis_ansi_string	*a;

	a = dst;
	if (a == NULL)
		return;
	if (src == NULL) {
		a->nas_len = a->nas_maxlen = 0;
		a->nas_buf = NULL;
	} else {
		a->nas_buf = src;
		a->nas_len = a->nas_maxlen = strlen(src);
	}

	return;
}

__stdcall static void
ntoskrnl_init_unicode_string(ndis_unicode_string *dst, uint16_t *src)
{
	ndis_unicode_string	*u;
	int			i;

	u = dst;
	if (u == NULL)
		return;
	if (src == NULL) {
		u->nus_len = u->nus_maxlen = 0;
		u->nus_buf = NULL;
	} else {
		i = 0;
		while(src[i] != 0)
			i++;
		u->nus_buf = src;
		u->nus_len = u->nus_maxlen = i * 2;
	}

	return;
}

__stdcall ndis_status
ntoskrnl_unicode_to_int(ndis_unicode_string *ustr, uint32_t base,
			uint32_t *val)
{
	uint16_t		*uchr;
	int			len, neg = 0;
	char			abuf[64];
	char			*astr;

	uchr = ustr->nus_buf;
	len = ustr->nus_len;
	bzero(abuf, sizeof(abuf));

	if ((char)((*uchr) & 0xFF) == '-') {
		neg = 1;
		uchr++;
		len -= 2;
	} else if ((char)((*uchr) & 0xFF) == '+') {
		neg = 0;
		uchr++;
		len -= 2;
	}

	if (base == 0) {
		if ((char)((*uchr) & 0xFF) == 'b') {
			base = 2;
			uchr++;
			len -= 2;
		} else if ((char)((*uchr) & 0xFF) == 'o') {
			base = 8;
			uchr++;
			len -= 2;
		} else if ((char)((*uchr) & 0xFF) == 'x') {
			base = 16;
			uchr++;
			len -= 2;
		} else
			base = 10;
	}

	astr = abuf;
	if (neg) {
		strcpy(astr, "-");
		astr++;
	}

	ndis_unicode_to_ascii(uchr, len, &astr);
	*val = strtoul(abuf, NULL, base);

	return(NDIS_STATUS_SUCCESS);
}

__stdcall static void
ntoskrnl_free_unicode_string(ndis_unicode_string *ustr)
{
	if (ustr->nus_buf == NULL)
		return;
	kfree(ustr->nus_buf, M_DEVBUF);
	ustr->nus_buf = NULL;
	return;
}

__stdcall static void
ntoskrnl_free_ansi_string(ndis_ansi_string *astr)
{
	if (astr->nas_buf == NULL)
		return;
	kfree(astr->nas_buf, M_DEVBUF);
	astr->nas_buf = NULL;
	return;
}

static int
atoi(const char *str)
{
	return (int)strtol(str, NULL, 10);
}

static long
atol(const char *str)
{
	return strtol(str, NULL, 10);
}

static int
rand(void)
{
	struct timeval		tv;

	microtime(&tv);
	skrandom(tv.tv_usec);
	return((int)krandom());
}

__stdcall static uint8_t
ntoskrnl_wdmver(uint8_t major, uint8_t minor)
{
	if (major == WDM_MAJOR && minor == WDM_MINOR_WINXP)
		return(TRUE);
	return(FALSE);
}

__stdcall static ndis_status
ntoskrnl_devprop(device_object *devobj, uint32_t regprop, uint32_t buflen,
		 void *prop, uint32_t *reslen)
{
	ndis_miniport_block	*block;

	block = devobj->do_rsvd;

	switch (regprop) {
	case DEVPROP_DRIVER_KEYNAME:
		ndis_ascii_to_unicode(__DECONST(char *,
		    device_get_nameunit(block->nmb_dev)), (uint16_t **)&prop);
		*reslen = strlen(device_get_nameunit(block->nmb_dev)) * 2;
		break;
	default:
		return(STATUS_INVALID_PARAMETER_2);
		break;
	}

	return(STATUS_SUCCESS);
}

__stdcall static void
ntoskrnl_init_mutex(kmutant *kmutex, uint32_t level)
{
	INIT_LIST_HEAD((&kmutex->km_header.dh_waitlisthead));
	kmutex->km_abandoned = FALSE;
	kmutex->km_apcdisable = 1;
	kmutex->km_header.dh_sigstate = TRUE;
	kmutex->km_header.dh_type = EVENT_TYPE_SYNC;
	kmutex->km_header.dh_size = OTYPE_MUTEX;
	kmutex->km_acquirecnt = 0;
	kmutex->km_ownerthread = NULL;
	return;
}

__stdcall static uint32_t
ntoskrnl_release_mutex(kmutant *kmutex, uint8_t kwait)
{
	lwkt_gettoken(&ntoskrnl_dispatchtoken);
	if (kmutex->km_ownerthread != curthread->td_proc) {
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		return(STATUS_MUTANT_NOT_OWNED);
	}
	kmutex->km_acquirecnt--;
	if (kmutex->km_acquirecnt == 0) {
		kmutex->km_ownerthread = NULL;
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
		ntoskrnl_wakeup(&kmutex->km_header);
	} else {
		lwkt_reltoken(&ntoskrnl_dispatchtoken);
	}

	return(kmutex->km_acquirecnt);
}

__stdcall static uint32_t
ntoskrnl_read_mutex(kmutant *kmutex)
{
	return(kmutex->km_header.dh_sigstate);
}

__stdcall void
ntoskrnl_init_event(nt_kevent *kevent, uint32_t type, uint8_t state)
{
	INIT_LIST_HEAD((&kevent->k_header.dh_waitlisthead));
	kevent->k_header.dh_sigstate = state;
	kevent->k_header.dh_type = type;
	kevent->k_header.dh_size = OTYPE_EVENT;
	return;
}

__stdcall uint32_t
ntoskrnl_reset_event(nt_kevent *kevent)
{
	uint32_t		prevstate;

	lwkt_gettoken(&ntoskrnl_dispatchtoken);
	prevstate = kevent->k_header.dh_sigstate;
	kevent->k_header.dh_sigstate = FALSE;
	lwkt_reltoken(&ntoskrnl_dispatchtoken);

	return(prevstate);
}

__stdcall uint32_t
ntoskrnl_set_event(nt_kevent *kevent, uint32_t increment, uint8_t kwait)
{
	uint32_t		prevstate;

	prevstate = kevent->k_header.dh_sigstate;
	ntoskrnl_wakeup(&kevent->k_header);

	return(prevstate);
}

__stdcall void
ntoskrnl_clear_event(nt_kevent *kevent)
{
	kevent->k_header.dh_sigstate = FALSE;
	return;
}

__stdcall uint32_t
ntoskrnl_read_event(nt_kevent *kevent)
{
	return(kevent->k_header.dh_sigstate);
}

__stdcall static ndis_status
ntoskrnl_objref(ndis_handle handle, uint32_t reqaccess, void *otype,
		uint8_t accessmode, void **object, void **handleinfo)
{
	nt_objref		*nr;

	nr = kmalloc(sizeof(nt_objref), M_DEVBUF, M_WAITOK|M_ZERO);

	INIT_LIST_HEAD((&nr->no_dh.dh_waitlisthead));
	nr->no_obj = handle;
	nr->no_dh.dh_size = OTYPE_THREAD;
	TAILQ_INSERT_TAIL(&ntoskrnl_reflist, nr, link);
	*object = nr;

	return(NDIS_STATUS_SUCCESS);
}

__stdcall __regcall static void
ntoskrnl_objderef(REGARGS1(void *object))
{
	nt_objref		*nr;

	nr = object;
	TAILQ_REMOVE(&ntoskrnl_reflist, nr, link);
	kfree(nr, M_DEVBUF);

	return;
}

__stdcall static uint32_t
ntoskrnl_zwclose(ndis_handle handle)
{
	return(STATUS_SUCCESS);
}

/*
 * This is here just in case the thread returns without calling
 * PsTerminateSystemThread().
 */
static void
ntoskrnl_thrfunc(void *arg)
{
	thread_context		*thrctx;
	__stdcall uint32_t (*tfunc)(void *);
	void			*tctx;
	uint32_t		rval;

	get_mplock();

	thrctx = arg;
	tfunc = thrctx->tc_thrfunc;
	tctx = thrctx->tc_thrctx;
	kfree(thrctx, M_TEMP);

	rval = tfunc(tctx);
	ntoskrnl_thread_exit(rval);
	/* not reached */
}

__stdcall static ndis_status
ntoskrnl_create_thread(ndis_handle *handle, uint32_t reqaccess,
		       void *objattrs, ndis_handle phandle,
		       void *clientid, void *thrfunc, void *thrctx)
{
	int			error;
	char			tname[128];
	thread_context		*tc;
	thread_t		td;

	tc = kmalloc(sizeof(thread_context), M_TEMP, M_WAITOK);

	tc->tc_thrctx = thrctx;
	tc->tc_thrfunc = thrfunc;

	ksprintf(tname, "windows kthread %d", ntoskrnl_kth);
	error = kthread_create_stk(ntoskrnl_thrfunc, tc, &td,
	    NDIS_KSTACK_PAGES * PAGE_SIZE, tname);
	*handle = td;

	ntoskrnl_kth++;

	return(error);
}

/*
 * In Windows, the exit of a thread is an event that you're allowed
 * to wait on, assuming you've obtained a reference to the thread using
 * ObReferenceObjectByHandle(). Unfortunately, the only way we can
 * simulate this behavior is to register each thread we create in a
 * reference list, and if someone holds a reference to us, we poke
 * them.
 */
__stdcall static ndis_status
ntoskrnl_thread_exit(ndis_status status)
{
	struct nt_objref	*nr;

	TAILQ_FOREACH(nr, &ntoskrnl_reflist, link) {
		if (nr->no_obj != curthread)
			continue;
		ntoskrnl_wakeup(&nr->no_dh);
		break;
	}

	ntoskrnl_kth--;

	rel_mplock();
	kthread_exit();	/* call explicitly */

	return(0);	/* notreached */
}

static uint32_t
ntoskrnl_dbgprint(char *fmt, ...)
{
	__va_list			ap;

	if (bootverbose) {
		__va_start(ap, fmt);
		kvprintf(fmt, ap);
	}

	return(STATUS_SUCCESS);
}

__stdcall static void
ntoskrnl_debugger(void)
{

#if __FreeBSD_version < 502113
	Debugger("ntoskrnl_debugger(): breakpoint");
#else
	kdb_enter("ntoskrnl_debugger(): breakpoint");
#endif
}

static void
ntoskrnl_timercall(void *arg)
{
	ktimer			*timer;

	timer = arg;

	timer->k_header.dh_inserted = FALSE;

	/*
	 * If this is a periodic timer, re-arm it
	 * so it will fire again. We do this before
	 * calling any deferred procedure calls because
	 * it's possible the DPC might cancel the timer,
	 * in which case it would be wrong for us to
	 * re-arm it again afterwards.
	 */

	if (timer->k_period) {
		timer->k_header.dh_inserted = TRUE;
		callout_reset(timer->k_handle, 1 + timer->k_period * hz / 1000,
			      ntoskrnl_timercall, timer);
	} else {
		callout_deactivate(timer->k_handle);
		kfree(timer->k_handle, M_NDIS);
		timer->k_handle = NULL;
	}

	if (timer->k_dpc != NULL)
		ntoskrnl_queue_dpc(timer->k_dpc, NULL, NULL);

	ntoskrnl_wakeup(&timer->k_header);
}

__stdcall void
ntoskrnl_init_timer(ktimer *timer)
{
	if (timer == NULL)
		return;

	ntoskrnl_init_timer_ex(timer,  EVENT_TYPE_NOTIFY);
}

__stdcall void
ntoskrnl_init_timer_ex(ktimer *timer, uint32_t type)
{
	if (timer == NULL)
		return;

	INIT_LIST_HEAD((&timer->k_header.dh_waitlisthead));
	timer->k_header.dh_sigstate = FALSE;
	timer->k_header.dh_inserted = FALSE;
	timer->k_header.dh_type = type;
	timer->k_header.dh_size = OTYPE_TIMER;
	timer->k_handle = NULL;

	return;
}

/*
 * This is a wrapper for Windows deferred procedure calls that
 * have been placed on an NDIS thread work queue. We need it
 * since the DPC could be a _stdcall function. Also, as far as
 * I can tell, defered procedure calls must run at DISPATCH_LEVEL.
 */
static void
ntoskrnl_run_dpc(void *arg)
{
	kdpc_func		dpcfunc;
	kdpc			*dpc;
	uint8_t			irql;

	dpc = arg;
	dpcfunc = (kdpc_func)dpc->k_deferedfunc;
	irql = FASTCALL1(hal_raise_irql, DISPATCH_LEVEL);
	dpcfunc(dpc, dpc->k_deferredctx, dpc->k_sysarg1, dpc->k_sysarg2);
	FASTCALL1(hal_lower_irql, irql);

	return;
}

__stdcall void
ntoskrnl_init_dpc(kdpc *dpc, void *dpcfunc, void *dpcctx)
{
	if (dpc == NULL)
		return;

	dpc->k_deferedfunc = dpcfunc;
	dpc->k_deferredctx = dpcctx;

	return;
}

__stdcall uint8_t
ntoskrnl_queue_dpc(kdpc *dpc, void *sysarg1, void *sysarg2)
{
	dpc->k_sysarg1 = sysarg1;
	dpc->k_sysarg2 = sysarg2;
	if (ndis_sched(ntoskrnl_run_dpc, dpc, NDIS_SWI))
		return(FALSE);

	return(TRUE);
}

__stdcall uint8_t
ntoskrnl_dequeue_dpc(kdpc *dpc)
{
	if (ndis_unsched(ntoskrnl_run_dpc, dpc, NDIS_SWI))
		return(FALSE);

	return(TRUE);
}

__stdcall uint8_t
ntoskrnl_set_timer_ex(ktimer *timer, int64_t duetime, uint32_t period,
		      kdpc *dpc)
{
	struct timeval		tv;
	uint64_t		curtime;
	uint8_t			pending;
	int			ticks;

	if (timer == NULL)
		return(FALSE);

	if (timer->k_header.dh_inserted == TRUE) {
		if (timer->k_handle != NULL)
			callout_stop(timer->k_handle);
		timer->k_header.dh_inserted = FALSE;
		pending = TRUE;
	} else
		pending = FALSE;

	timer->k_duetime = duetime;
	timer->k_period = period;
	timer->k_header.dh_sigstate = FALSE;
	timer->k_dpc = dpc;

	if (duetime < 0) {
		tv.tv_sec = - (duetime) / 10000000;
		tv.tv_usec = (- (duetime) / 10) -
		    (tv.tv_sec * 1000000);
	} else {
		ntoskrnl_time(&curtime);
		if (duetime < curtime)
			tv.tv_sec = tv.tv_usec = 0;
		else {
			tv.tv_sec = ((duetime) - curtime) / 10000000;
			tv.tv_usec = ((duetime) - curtime) / 10 -
			    (tv.tv_sec * 1000000);
		}
	}

	ticks = 1 + tv.tv_sec * hz + tv.tv_usec * hz / 1000000;
	timer->k_header.dh_inserted = TRUE;
	if (timer->k_handle == NULL) {
		timer->k_handle = kmalloc(sizeof(struct callout), M_NDIS,
					 M_INTWAIT);
		callout_init(timer->k_handle);
	}
	callout_reset(timer->k_handle, ticks, ntoskrnl_timercall, timer);

	return(pending);
}

__stdcall uint8_t
ntoskrnl_set_timer(ktimer *timer, int64_t duetime, kdpc *dpc)
{
	return (ntoskrnl_set_timer_ex(timer, duetime, 0, dpc));
}

__stdcall uint8_t
ntoskrnl_cancel_timer(ktimer *timer)
{
	uint8_t			pending;

	if (timer == NULL)
		return(FALSE);

	if (timer->k_header.dh_inserted == TRUE) {
		if (timer->k_handle != NULL) {
			callout_stop(timer->k_handle);
			kfree(timer->k_handle, M_NDIS);
			timer->k_handle = NULL;
		}
		if (timer->k_dpc != NULL)
			ntoskrnl_dequeue_dpc(timer->k_dpc);
		pending = TRUE;
	} else
		pending = FALSE;


	return(pending);
}

__stdcall uint8_t
ntoskrnl_read_timer(ktimer *timer)
{
	return(timer->k_header.dh_sigstate);
}

__stdcall static void
dummy(void)
{
	kprintf ("ntoskrnl dummy called...\n");
	return;
}


image_patch_table ntoskrnl_functbl[] = {
	{ "RtlCompareMemory",		(FUNC)ntoskrnl_memcmp },
	{ "RtlEqualUnicodeString",	(FUNC)ntoskrnl_unicode_equal },
	{ "RtlCopyUnicodeString",	(FUNC)ntoskrnl_unicode_copy },
	{ "RtlUnicodeStringToAnsiString", (FUNC)ntoskrnl_unicode_to_ansi },
	{ "RtlAnsiStringToUnicodeString", (FUNC)ntoskrnl_ansi_to_unicode },
	{ "RtlInitAnsiString",		(FUNC)ntoskrnl_init_ansi_string },
	{ "RtlInitUnicodeString",	(FUNC)ntoskrnl_init_unicode_string },
	{ "RtlFreeAnsiString",		(FUNC)ntoskrnl_free_ansi_string },
	{ "RtlFreeUnicodeString",	(FUNC)ntoskrnl_free_unicode_string },
	{ "RtlUnicodeStringToInteger",	(FUNC)ntoskrnl_unicode_to_int },
	{ "sprintf",			(FUNC)ksprintf },
	{ "vsprintf",			(FUNC)kvsprintf },
	{ "_snprintf",			(FUNC)ksnprintf },
	{ "_vsnprintf",			(FUNC)kvsnprintf },
	{ "DbgPrint",			(FUNC)ntoskrnl_dbgprint },
	{ "DbgBreakPoint",		(FUNC)ntoskrnl_debugger },
	{ "strncmp",			(FUNC)strncmp },
	{ "strcmp",			(FUNC)strcmp },
	{ "strncpy",			(FUNC)strncpy },
	{ "strcpy",			(FUNC)strcpy },
	{ "strlen",			(FUNC)strlen },
	{ "memcpy",			(FUNC)memcpy },
	{ "memmove",			(FUNC)memcpy },
	{ "memset",			(FUNC)memset },
	{ "IofCallDriver",		(FUNC)ntoskrnl_iofcalldriver },
	{ "IofCompleteRequest",		(FUNC)ntoskrnl_iofcompletereq },
	{ "IoBuildSynchronousFsdRequest", (FUNC)ntoskrnl_iobuildsynchfsdreq },
	{ "KeWaitForSingleObject",	(FUNC)ntoskrnl_waitforobj },
	{ "KeWaitForMultipleObjects",	(FUNC)ntoskrnl_waitforobjs },
	{ "_allmul",			(FUNC)_allmul },
	{ "_alldiv",			(FUNC)_alldiv },
	{ "_allrem",			(FUNC)_allrem },
	{ "_allshr",			(FUNC)_allshr },
	{ "_allshl",			(FUNC)_allshl },
	{ "_aullmul",			(FUNC)_aullmul },
	{ "_aulldiv",			(FUNC)_aulldiv },
	{ "_aullrem",			(FUNC)_aullrem },
	{ "_aullshr",			(FUNC)_aullshr },
	{ "_aullshl",			(FUNC)_aullshl },
	{ "atoi",			(FUNC)atoi },
	{ "atol",			(FUNC)atol },
	{ "rand",			(FUNC)rand },
	{ "WRITE_REGISTER_USHORT",	(FUNC)ntoskrnl_writereg_ushort },
	{ "READ_REGISTER_USHORT",	(FUNC)ntoskrnl_readreg_ushort },
	{ "WRITE_REGISTER_ULONG",	(FUNC)ntoskrnl_writereg_ulong },
	{ "READ_REGISTER_ULONG",	(FUNC)ntoskrnl_readreg_ulong },
	{ "READ_REGISTER_UCHAR",	(FUNC)ntoskrnl_readreg_uchar },
	{ "WRITE_REGISTER_UCHAR",	(FUNC)ntoskrnl_writereg_uchar },
	{ "ExInitializePagedLookasideList", (FUNC)ntoskrnl_init_lookaside },
	{ "ExDeletePagedLookasideList", (FUNC)ntoskrnl_delete_lookaside },
	{ "ExInitializeNPagedLookasideList", (FUNC)ntoskrnl_init_nplookaside },
	{ "ExDeleteNPagedLookasideList", (FUNC)ntoskrnl_delete_nplookaside },
	{ "InterlockedPopEntrySList",	(FUNC)ntoskrnl_pop_slist },
	{ "InterlockedPushEntrySList",	(FUNC)ntoskrnl_push_slist },
	{ "ExInterlockedPopEntrySList",	(FUNC)ntoskrnl_pop_slist_ex },
	{ "ExInterlockedPushEntrySList",(FUNC)ntoskrnl_push_slist_ex },
	{ "KefAcquireSpinLockAtDpcLevel", (FUNC)ntoskrnl_lock_dpc },
	{ "KefReleaseSpinLockFromDpcLevel", (FUNC)ntoskrnl_unlock_dpc },
	{ "InterlockedIncrement",	(FUNC)ntoskrnl_interlock_inc },
	{ "InterlockedDecrement",	(FUNC)ntoskrnl_interlock_dec },
	{ "ExInterlockedAddLargeStatistic",
					(FUNC)ntoskrnl_interlock_addstat },
	{ "IoFreeMdl",			(FUNC)ntoskrnl_freemdl },
	{ "MmSizeOfMdl",		(FUNC)ntoskrnl_sizeofmdl },
	{ "MmMapLockedPages",		(FUNC)ntoskrnl_mmaplockedpages },
	{ "MmMapLockedPagesSpecifyCache",
					(FUNC)ntoskrnl_mmaplockedpages_cache },
	{ "MmUnmapLockedPages",		(FUNC)ntoskrnl_munmaplockedpages },
	{ "MmBuildMdlForNonPagedPool",	(FUNC)ntoskrnl_build_npaged_mdl },
	{ "KeInitializeSpinLock",	(FUNC)ntoskrnl_init_lock },
	{ "IoIsWdmVersionAvailable",	(FUNC)ntoskrnl_wdmver },
	{ "IoGetDeviceProperty",	(FUNC)ntoskrnl_devprop },
	{ "KeInitializeMutex",		(FUNC)ntoskrnl_init_mutex },
	{ "KeReleaseMutex",		(FUNC)ntoskrnl_release_mutex },
	{ "KeReadStateMutex",		(FUNC)ntoskrnl_read_mutex },
	{ "KeInitializeEvent",		(FUNC)ntoskrnl_init_event },
	{ "KeSetEvent",			(FUNC)ntoskrnl_set_event },
	{ "KeResetEvent",		(FUNC)ntoskrnl_reset_event },
	{ "KeClearEvent",		(FUNC)ntoskrnl_clear_event },
	{ "KeReadStateEvent",		(FUNC)ntoskrnl_read_event },
	{ "KeInitializeTimer",		(FUNC)ntoskrnl_init_timer },
	{ "KeInitializeTimerEx",	(FUNC)ntoskrnl_init_timer_ex },
	{ "KeSetTimer",			(FUNC)ntoskrnl_set_timer },
	{ "KeSetTimerEx",		(FUNC)ntoskrnl_set_timer_ex },
	{ "KeCancelTimer",		(FUNC)ntoskrnl_cancel_timer },
	{ "KeReadStateTimer",		(FUNC)ntoskrnl_read_timer },
	{ "KeInitializeDpc",		(FUNC)ntoskrnl_init_dpc },
	{ "KeInsertQueueDpc",		(FUNC)ntoskrnl_queue_dpc },
	{ "KeRemoveQueueDpc",		(FUNC)ntoskrnl_dequeue_dpc },
	{ "ObReferenceObjectByHandle",	(FUNC)ntoskrnl_objref },
	{ "ObfDereferenceObject",	(FUNC)ntoskrnl_objderef },
	{ "ZwClose",			(FUNC)ntoskrnl_zwclose },
	{ "PsCreateSystemThread",	(FUNC)ntoskrnl_create_thread },
	{ "PsTerminateSystemThread",	(FUNC)ntoskrnl_thread_exit },

	/*
	 * This last entry is a catch-all for any function we haven't
	 * implemented yet. The PE import list patching routine will
	 * use it for any function that doesn't have an explicit match
	 * in this table.
	 */

	{ NULL, (FUNC)dummy },

	/* End of list. */

	{ NULL, NULL },
};
