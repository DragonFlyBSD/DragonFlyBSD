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

extern int DebugOpt;
extern int VerboseOpt;
extern int QuietOpt;
extern int NormalExit;

int hammer2_ioctl_handle(const char *sel_path);
void hammer2_demon(void *(*func)(void *), void *arg);
void hammer2_bswap_head(hammer2_msg_hdr_t *head);

int cmd_remote_connect(const char *sel_path, const char *url);
int cmd_remote_disconnect(const char *sel_path, const char *url);
int cmd_remote_status(const char *sel_path, int all_opt);

int cmd_pfs_list(const char *sel_path);
int cmd_pfs_create(const char *sel_path, const char *name,
			uint8_t pfs_type, const char *uuid_str);
int cmd_pfs_delete(const char *sel_path, const char *name);

int cmd_service(void);
int cmd_stat(int ac, const char **av);
int cmd_leaf(const char *sel_path);
int cmd_shell(const char *hostname);
int cmd_show(const char *devpath);
int cmd_rsainit(const char *dir_path);
int cmd_rsaenc(const char **keys, int nkeys);
int cmd_rsadec(const char **keys, int nkeys);

void hammer2_ioq_init(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_ioq_done(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_iocom_init(hammer2_iocom_t *iocom, int sock_fd, int alt_fd);
void hammer2_iocom_done(hammer2_iocom_t *iocom);
hammer2_msg_t *hammer2_msg_alloc(hammer2_iocom_t *iocom, size_t aux_size,
			uint32_t cmd);
void hammer2_msg_reply(hammer2_iocom_t *iocom, hammer2_msg_t *msg,
			uint16_t error);
void hammer2_msg_free(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

void hammer2_iocom_core(hammer2_iocom_t *iocom,
			void (*iocom_recvmsg)(hammer2_iocom_t *),
			void (*iocom_sendmsg)(hammer2_iocom_t *),
			void (*iocom_altmsg)(hammer2_iocom_t *));
hammer2_msg_t *hammer2_ioq_read(hammer2_iocom_t *iocom);
void hammer2_ioq_write(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

void hammer2_iocom_drain(hammer2_iocom_t *iocom);
void hammer2_iocom_flush(hammer2_iocom_t *iocom);

void hammer2_state_cleanuprx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
void hammer2_state_free(hammer2_state_t *state);

void hammer2_crypto_negotiate(hammer2_iocom_t *iocom);
void hammer2_crypto_decrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq);
void hammer2_crypto_decrypt_aux(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			hammer2_msg_t *msg, int already);
int hammer2_crypto_encrypt(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			struct iovec *iov, int n);
void hammer2_crypto_encrypt_wrote(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq,
			int nact);

const char *hammer2_time64_to_str(uint64_t htime64, char **strp);
const char *hammer2_uuid_to_str(uuid_t *uuid, char **strp);
const char *hammer2_iptype_to_str(uint8_t type);
const char *hammer2_pfstype_to_str(uint8_t type);
const char *sizetostr(hammer2_off_t size);

void hammer2_shell_remote(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
void iocom_printf(hammer2_iocom_t *iocom, uint32_t cmd, const char *ctl, ...);
