# Threading implementation, merged from lib/libthread_xu.

CMAPS+=	${.CURDIR}/thread/Symbol.map

thread_CFLAGS=	-DPTHREAD_KERNEL -D_THREAD_SAFE
thread_CFLAGS+=	-I${.CURDIR}/thread/${MACHINE_ARCH}
thread_CFLAGS+=	-I${.CURDIR}/../libthread_db
thread_CFLAGS+=	-I${.CURDIR}/../../libexec/rtld-elf

# enable extra internal consistancy checks
thread_CFLAGS+=	-D_PTHREADS_INVARIANTS

# debugging
.if defined(PTHREADS_DEBUGGING)
thread_CFLAGS+=	-D_PTHREADS_DEBUGGING
.endif
.if defined(PTHREADS_DEBUGGING2)
thread_CFLAGS+=	-D_PTHREADS_DEBUGGING2
.endif

.PATH: ${.CURDIR}/thread
thread_SRCS=	thr_affinity.c thr_attr.c \
		thr_barrier.c thr_barrierattr.c \
		thr_cancel.c thr_clean.c thr_concurrency.c thr_cond.c \
		thr_condattr.c thr_create.c thr_ctrdtr.c \
		thr_detach.c thr_distribute.c \
		thr_equal.c thr_event.c thr_exit.c \
		thr_fork.c \
		thr_getcpuclockid.c thr_getprio.c thr_getschedparam.c \
		thr_getthreadid_np.c \
		thr_info.c thr_init.c thr_isthreaded_np.c \
		thr_join.c \
		thr_kern.c thr_kill.c \
		thr_list.c \
		thr_main_np.c thr_malloc.c \
		thr_multi_np.c thr_mutex.c thr_mutexattr.c \
		thr_once.c \
		thr_printf.c thr_pspinlock.c \
		thr_resume_np.c thr_rtld.c thr_rwlock.c thr_rwlockattr.c \
		thr_self.c thr_sem.c thr_setprio.c thr_setschedparam.c \
		thr_sig.c thr_single_np.c thr_spec.c thr_spinlock.c \
		thr_stack.c thr_suspend_np.c thr_switch_np.c \
		thr_symbols.c thr_syscalls.c \
		thr_umtx.c \
		thr_yield.c

.PATH: ${.CURDIR}/thread/${MACHINE_ARCH}
thread_SRCS+=	_umtx_sleep_wakeup_err.S

SRCS+=		${thread_SRCS}

FLAGS_GROUPS+=		thread
thread_FLAGS=		${thread_CFLAGS}
thread_FLAGS_FILES=	${thread_SRCS}
