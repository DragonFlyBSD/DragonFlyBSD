/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 */

/*
 * Rollup headers for hammer2 utility
 */
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/tty.h>
#include <sys/endian.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vfs/hammer2/hammer2_disk.h>
#include <vfs/hammer2/hammer2_mount.h>
#include <vfs/hammer2/hammer2_ioctl.h>
#include <vfs/hammer2/hammer2_network.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <uuid.h>
#include <assert.h>
#include <pthread.h>
#include <poll.h>

#include <libutil.h>

#include "network.h"

#define HAMMER2_DEFAULT_DIR	"/etc/hammer2"
#define HAMMER2_PATH_REMOTE	HAMMER2_DEFAULT_DIR "/remote"

struct hammer2_idmap {
	struct hammer2_idmap *next;
	uint32_t ran_beg;	/* inclusive */
	uint32_t ran_end;	/* inclusive */
};

typedef struct hammer2_idmap hammer2_idmap_t;

extern int DebugOpt;
extern int VerboseOpt;
extern int QuietOpt;
extern int NormalExit;

/*
 * Hammer2 command APIs
 */
int hammer2_ioctl_handle(const char *sel_path);
void hammer2_demon(void *(*func)(void *), void *arg);

int cmd_remote_connect(const char *sel_path, const char *url);
int cmd_remote_disconnect(const char *sel_path, const char *url);
int cmd_remote_status(const char *sel_path, int all_opt);

int cmd_pfs_getid(const char *sel_path, const char *name, int privateid);
int cmd_pfs_list(const char *sel_path);
int cmd_pfs_create(const char *sel_path, const char *name,
			uint8_t pfs_type, const char *uuid_str);
int cmd_pfs_delete(const char *sel_path, const char *name);

int cmd_service(void);
int cmd_stat(int ac, const char **av);
int cmd_leaf(const char *sel_path);
int cmd_shell(const char *hostname);
int cmd_debugspan(const char *hostname);
int cmd_show(const char *devpath);
int cmd_rsainit(const char *dir_path);
int cmd_rsaenc(const char **keys, int nkeys);
int cmd_rsadec(const char **keys, int nkeys);

/*
 * Msg support functions
 */
void hammer2_bswap_head(hammer2_msg_hdr_t *head);
void hammer2_ioq_init(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_ioq_done(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_iocom_init(hammer2_iocom_t *iocom, int sock_fd, int alt_fd,
			void (*state_func)(hammer2_iocom_t *),
			void (*rcvmsg_func)(hammer2_iocom_t *, hammer2_msg_t *),
			void (*altmsg_func)(hammer2_iocom_t *));
void hammer2_iocom_restate(hammer2_iocom_t *iocom,
			void (*state_func)(hammer2_iocom_t *),
			void (*rcvmsg_func)(hammer2_iocom_t *, hammer2_msg_t *),
			void (*altmsg_func)(hammer2_iocom_t *));
void hammer2_iocom_done(hammer2_iocom_t *iocom);
hammer2_msg_t *hammer2_msg_alloc(hammer2_iocom_t *iocom, size_t aux_size,
			uint32_t cmd);
void hammer2_msg_reply(hammer2_iocom_t *iocom, hammer2_msg_t *msg,
			uint32_t error);
void hammer2_msg_result(hammer2_iocom_t *iocom, hammer2_msg_t *msg,
			uint32_t error);
void hammer2_state_reply(hammer2_state_t *state, uint32_t error);

void hammer2_msg_free(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

void hammer2_iocom_core(hammer2_iocom_t *iocom);
hammer2_msg_t *hammer2_ioq_read(hammer2_iocom_t *iocom);
void hammer2_msg_write(hammer2_iocom_t *iocom, hammer2_msg_t *msg,
			void (*func)(hammer2_state_t *, hammer2_msg_t *),
			void *data, hammer2_state_t **statep);

void hammer2_iocom_drain(hammer2_iocom_t *iocom);
void hammer2_iocom_flush1(hammer2_iocom_t *iocom);
void hammer2_iocom_flush2(hammer2_iocom_t *iocom);

void hammer2_state_cleanuprx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
void hammer2_state_free(hammer2_state_t *state);

/*
 * Msg protocol functions
 */
void hammer2_msg_lnk(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
void hammer2_msg_dbg(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

/*
 * Crypto functions
 */
void hammer2_crypto_setup(void);
void hammer2_crypto_negotiate(hammer2_iocom_t *iocom);
void hammer2_crypto_decrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_crypto_decrypt_aux(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			hammer2_msg_t *msg, int already);
int hammer2_crypto_encrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			struct iovec *iov, int n, size_t *nmaxp);
void hammer2_crypto_encrypt_wrote(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			int nact);

/*
 * Misc functions
 */
const char *hammer2_time64_to_str(uint64_t htime64, char **strp);
const char *hammer2_uuid_to_str(uuid_t *uuid, char **strp);
const char *hammer2_iptype_to_str(uint8_t type);
const char *hammer2_pfstype_to_str(uint8_t type);
const char *sizetostr(hammer2_off_t size);
int hammer2_connect(const char *hostname);

void *master_service(void *data);

void hammer2_msg_debug(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
void iocom_printf(hammer2_iocom_t *iocom, uint32_t cmd, const char *ctl, ...);
void *hammer2_alloc(size_t bytes);
void hammer2_free(void *ptr);
