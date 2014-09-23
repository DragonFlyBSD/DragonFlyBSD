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
 * vinumext.h: external definitions
 */

extern struct _vinum_conf vinum_conf;	/* configuration information */

#ifdef VINUMDEBUG
extern int debug;			/* debug flags */
#endif

/*
 * Physical read and write drive
 */
#define read_drive(a, b, c, d) driveio (a, b, c, d, BUF_CMD_READ)
#define write_drive(a, b, c, d) driveio (a, b, c, d, BUF_CMD_WRITE)

#define CHECKALLOC(ptr, msg)		\
    if (ptr == NULL) {			\
	    kprintf (msg);		\
	    longjmp (command_fail, -1); \
    }

#ifndef _KERNEL
struct vnode;
struct proc;
#endif

#ifdef _KERNEL
int vinum_inactive(int);
void free_vinum(int);
int give_sd_to_plex(int plexno, int sdno);
void give_sd_to_drive(int sdno);
int give_plex_to_volume(int volno, int plexno);
struct drive *check_drive(char *);
enum drive_label_info read_drive_label(struct drive *, int);
int parse_config(char *, struct keywordset *, int);
int parse_user_config(char *cptr, struct keywordset *keyset);
u_int64_t sizespec(char *spec);
int volume_index(struct volume *volume);
int plex_index(struct plex *plex);
int sd_index(struct sd *sd);
int drive_index(struct drive *drive);
int my_plex(int volno, int plexno);
int my_sd(int plexno, int sdno);
int get_empty_drive(void);
int find_drive(const char *name, int create);
int find_drive_by_dev(const char *devname, int create);
int get_empty_sd(void);
int find_subdisk(const char *name, int create);
void return_drive_space(int driveno, int64_t offset, int length);
void free_sd(int sdno);
void free_volume(int volno);
int get_empty_plex(void);
int find_plex(const char *name, int create);
void free_plex(int plexno);
int get_empty_volume(void);
int find_volume(const char *name, int create);
void config_subdisk(int);
void config_plex(int);
void config_volume(int);
void config_drive(int);
void updateconfig(int);
void update_sd_config(int sdno, int kernelstate);
void update_plex_config(int plexno, int kernelstate);
void update_volume_config(int volno, int kernelstate);
void update_config(void);
void drive_io_done(struct buf *);
void save_config(void);
void daemon_save_config(void);
void write_config(char *, int);
int start_config(int);
void finish_config(int);
void remove(struct vinum_ioctl_msg *msg);
void remove_drive_entry(int driveno, int force);
void remove_sd_entry(int sdno, int force, int recurse);
void remove_plex_entry(int plexno, int force, int recurse);
void remove_volume_entry(int volno, int force, int recurse);

void checkdiskconfig(char *);
int open_drive(struct drive *, struct proc *, int);
void close_drive(struct drive *drive);
void close_locked_drive(struct drive *drive);
int driveio(struct drive *, char *, size_t, off_t, buf_cmd_t);
int set_drive_parms(struct drive *drive);
int init_drive(struct drive *, int);
/* void throw_rude_remark (int, struct _ioctl_reply *, char *, ...); XXX */
void throw_rude_remark(int, char *,...) __printflike(2, 3);

/* XXX die die */
void format_config(char *config, int len);
void checkkernel(char *op);
void free_drive(struct drive *drive);
void down_drive(struct drive *drive);
void remove_drive(int driveno);

int vinum_scandisk(char *drivename[], int drives);

/* I/O */
d_open_t vinumopen;
d_close_t vinumclose;
d_strategy_t vinumstrategy;
d_ioctl_t vinumioctl;
d_dump_t vinumdump;
d_psize_t vinumsize;
d_kqfilter_t vinumkqfilter;

int vinumfilt_rd(struct knote *, long);
int vinumfilt_wr(struct knote *, long);
void vinumfilt_detach(struct knote *);

int vinumstart(cdev_t dev, struct bio *bio, int reviveok);
int launch_requests(struct request *rq, int reviveok);
void sdio(struct bio *bio);

/* XXX Do we need this? */
int vinumpart(cdev_t);

extern jmp_buf command_fail;		/* return here if config fails */
extern struct dev_ops vinum_ops;

#ifdef VINUMDEBUG
/* Memory allocation and request tracing */
void vinum_meminfo(caddr_t data);
int vinum_mallocinfo(caddr_t data);
int vinum_rqinfo(caddr_t data);
void LongJmp(jmp_buf, int);
#else
void longjmp(jmp_buf, int);		/* the kernel doesn't define this */
#endif
int setjmp(jmp_buf);

char *basename(char *);
void expand_table(void **, int, int);

struct request;
struct rqgroup *allocrqg(struct request *rq, int elements);
void deallocrqg(struct rqgroup *rqg);

/* Device number decoding */
int Volno(cdev_t x);
int Plexno(cdev_t x);
int Sdno(cdev_t x);

/* State transitions */
int set_drive_state(int driveno, enum drivestate state, enum setstateflags flags);
int set_sd_state(int sdno, enum sdstate state, enum setstateflags flags);
enum requeststatus checksdstate(struct sd *sd, struct request *rq, vinum_off_t diskaddr, vinum_off_t diskend);
int set_plex_state(int plexno, enum plexstate state, enum setstateflags flags);
int set_volume_state(int volumeno, enum volumestate state, enum setstateflags flags);
void update_sd_state(int sdno);
void forceup(int plexno);
void update_plex_state(int plexno);
void update_volume_state(int volno);
void invalidate_subdisks(struct plex *, enum sdstate);
void start_object(struct vinum_ioctl_msg *);
void stop_object(struct vinum_ioctl_msg *);
void setstate(struct vinum_ioctl_msg *msg);
void setstate_by_force(struct vinum_ioctl_msg *msg);
void vinum_label(int);
int initsd(int, int);
struct buf *parityrebuild(struct plex *, vinum_off_t, int, enum parityop, struct rangelock **, off_t *);
enum requeststatus sddownstate(struct request *rq);

int restart_plex(int plexno);
int revive_read(struct sd *sd);
int revive_block(int sdno);
void parityops(struct vinum_ioctl_msg *);

/* Auxiliary functions */
enum sdstates sdstatemap(struct plex *plex);
enum volplexstate vpstate(struct plex *plex);
#endif

enum keyword get_keyword(char *, struct keywordset *);
void listconfig(void);
char *drive_state(enum drivestate);
char *volume_state(enum volumestate);
char *plex_state(enum plexstate);
char *plex_org(enum plexorg);
char *sd_state(enum sdstate);
enum drivestate DriveState(char *text);
enum sdstate SdState(char *text);
enum plexstate PlexState(char *text);
enum volumestate VolState(char *text);
struct drive *validdrive(int driveno, struct _ioctl_reply *);
struct sd *validsd(int sdno, struct _ioctl_reply *);
struct plex *validplex(int plexno, struct _ioctl_reply *);
struct volume *validvol(int volno, struct _ioctl_reply *);
int tokenize(char *, char *[]);
void resetstats(struct vinum_ioctl_msg *msg);

/* Locking */
#ifdef VINUMDEBUG
int lockdrive(struct drive *drive, char *, int);
#else
int lockdrive(struct drive *drive);
#endif
void unlockdrive(struct drive *drive);
int lockvol(struct volume *vol);
void unlockvol(struct volume *vol);
int lockplex(struct plex *plex);
void unlockplex(struct plex *plex);
struct rangelock *lockrange(vinum_off_t stripe, struct buf *bp, struct plex *plex);
int lock_config(void);
void unlock_config(void);

/* D�mon */

void vinum_daemon(void);
int vinum_finddaemon(void);
int vinum_setdaemonopts(int);

extern struct daemonq *daemonq;		/* daemon's work queue */
extern struct daemonq *dqend;		/* and the end of the queue */

#undef Free				/* defined in some funny net stuff */
#ifdef _KERNEL
#ifdef VINUMDEBUG
#define Malloc(x)  MMalloc ((x), __FILE__, __LINE__)
#define Free(x)	   FFree ((x), __FILE__, __LINE__)
caddr_t MMalloc(int size, char *, int);
void FFree(void *mem, char *, int);
#define LOCKDRIVE(d) lockdrive (d, __FILE__, __LINE__)
#else
#define Malloc(x)  kmalloc((x), M_DEVBUF, \
	mycpu->gd_intr_nesting_level == 0? M_WAITOK: M_INTWAIT)
#define Free(x)    kfree((x), M_DEVBUF)
#define LOCKDRIVE(d) lockdrive (d)
#endif
#else
#define Malloc(x)  malloc ((x))		/* just the size */
#define Free(x)	   free ((x))		/* just the address */
#endif
