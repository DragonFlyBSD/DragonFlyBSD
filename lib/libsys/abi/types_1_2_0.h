/*
 * TYPES_1_2_0.H
 *
 * This header file contains types specific to the 1.2.0 DragonFly release.
 *
 * WARNING!  This header file is parsed by a program to automatically
 * generate compatibility conversions.  Only internal type names may be
 * used.  e.g.  'char_ptr_t' instead of 'char *'.
 *
 * $DragonFly: src/lib/libsys/abi/types_1_2_0.h,v 1.1 2005/05/08 18:14:54 dillon Exp $
 */

struct rusage_1_2_0 {
};

struct statfs_1_2_0 {
};

struct msghdr_1_2_0 {
};

struct itimerval_1_2_0 {
};

struct timeval_1_2_0 {
};

struct sigvec_1_2_0 {
};

struct sigstack_1_2_0 {
};

struct timezone_1_2_0 {
};

struct iovec_1_2_0 {
};

struct statfs_1_2_0 { 
};

struct fhandle_1_2_0 {
};

struct utsname_1_2_0 { 
};

struct rtprio_1_2_0 {
};

struct timex_1_2_0 { 
};

struct stat_1_2_0 {
};

struct pollfs_1_2_0 {
};

struct sembuf_1_2_0 {
};

struct msqid_ds_1_2_0 {
};

struct shmid_ds_1_2_0 {
};

struct timespec_1_2_0 {
};

struct module_stat_1_2_0 {
};

struct kld_file_1_2_0 {
};

struct aiocb_1_2_0 {
};

struct sigevent_1_2_0 {
};

struct sched_param_1_2_0 {
};

struct jail_1_2_0 {
};

struct sigaction_1_2_0 {
};

struct acl_1_2_0 {
};

struct kevent_1_2_0 {
};

struct sf_hdtr_1_2_0 {
};

struct upcall_1_2_0 {
};

struct caps_msgid_1_2_0 {
};

struct caps_cred_1_2_0 {
};

struct tls_info_1_2_0 {
};

/*
 * The system calls configuration file parses types as single words.
 */
typedef struct rusage_1_2_0 *rusage_1_2_0_ptr;
typedef struct statfs_1_2_0 *statfs_1_2_0_ptr;
typedef struct msghdr_1_2_0 *msghdr_1_2_0_ptr;
typedef struct itimerval_1_2_0 *itimerval_1_2_0_ptr;
typedef struct timeval_1_2_0 *timeval_1_2_0_ptr;
typedef struct sigvec_1_2_0 *sigvec_1_2_0_ptr;
typedef struct sigstack_1_2_0 *sigstack_1_2_0_ptr;
typedef struct timezone_1_2_0 *timezone_1_2_0_ptr;
typedef struct iovec_1_2_0 *iovec_1_2_0_ptr;
typedef struct statfs_1_2_0 *statfs_1_2_0_ptr;
typedef struct fhandle_1_2_0 *fhandle_1_2_0_ptr;
typedef struct utsname_1_2_0 *utsname_1_2_0_ptr;
typedef struct rtprio_1_2_0 *rtprio_1_2_0_ptr;
typedef struct timex_1_2_0 *timex_1_2_0_ptr;
typedef struct stat_1_2_0 *stat_1_2_0_ptr;
typedef struct pollfs_1_2_0 *pollfs_1_2_0_ptr;
typedef struct sembuf_1_2_0 *sembuf_1_2_0_ptr;
typedef struct msqid_ds_1_2_0 *msqid_ds_1_2_0_ptr;
typedef struct shmid_ds_1_2_0 *shmid_ds_1_2_0_ptr;
typedef struct timespec_1_2_0 *timespec_1_2_0_ptr;
typedef struct module_stat_1_2_0 *module_stat_1_2_0_ptr;
typedef struct kld_file_1_2_0 *kld_file_1_2_0_ptr;
typedef struct aiocb_1_2_0 *aiocb_1_2_0_ptr;
typedef struct sigevent_1_2_0 *sigevent_1_2_0_ptr;
typedef struct sched_param_1_2_0 *sched_param_1_2_0_ptr;
typedef struct jail_1_2_0 *jail_1_2_0_ptr;
typedef struct sigaction_1_2_0 *sigaction_1_2_0_ptr;
typedef struct acl_1_2_0 *acl_1_2_0_ptr;
typedef struct kevent_1_2_0 *kevent_1_2_0_ptr;
typedef struct sf_hdtr_1_2_0 *sf_hdtr_1_2_0_ptr;
typedef struct upcall_1_2_0 *upcall_1_2_0_ptr;
typedef struct caps_msgid_1_2_0 *caps_msgid_1_2_0_ptr;
typedef struct caps_cred_1_2_0 *caps_cred_1_2_0_ptr;
typedef struct tls_info_1_2_0 *tls_info_1_2_0_ptr;

typedef const struct rusage_1_2_0 *const_rusage_1_2_0_ptr;
typedef const struct statfs_1_2_0 *const_statfs_1_2_0_ptr;
typedef const struct msghdr_1_2_0 *const_msghdr_1_2_0_ptr;
typedef const struct itimerval_1_2_0 *const_itimerval_1_2_0_ptr;
typedef const struct timeval_1_2_0 *const_timeval_1_2_0_ptr;
typedef const struct sigvec_1_2_0 *const_sigvec_1_2_0_ptr;
typedef const struct sigstack_1_2_0 *const_sigstack_1_2_0_ptr;
typedef const struct timezone_1_2_0 *const_timezone_1_2_0_ptr;
typedef const struct iovec_1_2_0 *const_iovec_1_2_0_ptr;
typedef const struct statfs_1_2_0 *const_statfs_1_2_0_ptr;
typedef const struct fhandle_1_2_0 *const_fhandle_1_2_0_ptr;
typedef const struct utsname_1_2_0 *const_utsname_1_2_0_ptr;
typedef const struct rtprio_1_2_0 *const_rtprio_1_2_0_ptr;
typedef const struct timex_1_2_0 *const_timex_1_2_0_ptr;
typedef const struct stat_1_2_0 *const_stat_1_2_0_ptr;
typedef const struct pollfs_1_2_0 *const_pollfs_1_2_0_ptr;
typedef const struct sembuf_1_2_0 *const_sembuf_1_2_0_ptr;
typedef const struct msqid_ds_1_2_0 *const_msqid_ds_1_2_0_ptr;
typedef const struct shmid_ds_1_2_0 *const_shmid_ds_1_2_0_ptr;
typedef const struct timespec_1_2_0 *const_timespec_1_2_0_ptr;
typedef const struct module_stat_1_2_0 *const_module_stat_1_2_0_ptr;
typedef const struct kld_file_1_2_0 *const_kld_file_1_2_0_ptr;
typedef const struct aiocb_1_2_0 *const_aiocb_1_2_0_ptr;
typedef const struct sigevent_1_2_0 *const_sigevent_1_2_0_ptr;
typedef const struct sched_param_1_2_0 *const_sched_param_1_2_0_ptr;
typedef const struct jail_1_2_0 *const_jail_1_2_0_ptr;
typedef const struct sigaction_1_2_0 *const_sigaction_1_2_0_ptr;
typedef const struct acl_1_2_0 *const_acl_1_2_0_ptr;
typedef const struct kevent_1_2_0 *const_kevent_1_2_0_ptr;
typedef const struct sf_hdtr_1_2_0 *const_sf_hdtr_1_2_0_ptr;
typedef const struct upcall_1_2_0 *const_upcall_1_2_0_ptr;
typedef const struct caps_msgid_1_2_0 *const_caps_msgid_1_2_0_ptr;
typedef const struct caps_cred_1_2_0 *const_caps_cred_1_2_0_ptr;
typedef const struct tls_info_1_2_0 *const_tls_info_1_2_0_ptr;

