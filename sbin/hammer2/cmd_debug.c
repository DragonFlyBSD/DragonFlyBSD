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

#include "hammer2.h"

#include <openssl/sha.h>

#define GIG	(1024LL*1024*1024)

static int show_tab = 2;
static int show_depth = -1;

static void shell_msghandler(dmsg_msg_t *msg, int unmanaged);
static void shell_ttymsg(dmsg_iocom_t *iocom);
static void CountBlocks(hammer2_bmap_data_t *bmap, int value,
		hammer2_off_t *accum16, hammer2_off_t *accum64);

/************************************************************************
 *				    SHELL				*
 ************************************************************************/

int
cmd_shell(const char *hostname)
{
	dmsg_master_service_info_t *info;
	pthread_t thread;
	int fd;

	fd = dmsg_connect(hostname);
	if (fd < 0)
		return 1;

	info = malloc(sizeof(*info));
	bzero(info, sizeof(*info));
	info->fd = fd;
	info->detachme = 0;
	info->usrmsg_callback = shell_msghandler;
	info->altmsg_callback = shell_ttymsg;
	info->label = strdup("debug");
	pthread_create(&thread, NULL, dmsg_master_service, info);
	pthread_join(thread, NULL);

	return 0;
}

#if 0
int
cmd_shell(const char *hostname)
{
	struct dmsg_iocom iocom;
	dmsg_msg_t *msg;
	int fd;

	/*
	 * Connect to the target
	 */
	fd = dmsg_connect(hostname);
	if (fd < 0)
		return 1;

	/*
	 * Initialize the session and transmit an empty DMSG_DBG_SHELL
	 * to cause the remote end to generate a prompt.
	 */
	dmsg_iocom_init(&iocom, fd, 0,
			NULL,
			shell_rcvmsg,
			hammer2_shell_parse,
			shell_ttymsg);
	fcntl(0, F_SETFL, O_NONBLOCK);
	printf("debug: connected\n");

	msg = dmsg_msg_alloc(&iocom.state0, 0, DMSG_DBG_SHELL, NULL, NULL);
	dmsg_msg_write(msg);
	dmsg_iocom_core(&iocom);
	fprintf(stderr, "debug: disconnected\n");
	close(fd);
	return 0;
}
#endif

/*
 * Debug session front-end
 *
 * Callback from dmsg_iocom_core() when messages might be present
 * on the socket.
 */
static
void
shell_msghandler(dmsg_msg_t *msg, int unmanaged)
{
	dmsg_msg_t *nmsg;

	switch(msg->tcmd) {
#if 0
	case DMSG_LNK_ERROR:
	case DMSG_LNK_ERROR | DMSGF_REPLY:
		/*
		 * One-way non-transactional LNK_ERROR messages typically
		 * indicate a connection failure.  Error code 0 is used by
		 * the debug shell to indicate no more results from last cmd.
		 */
		if (msg->any.head.error) {
			fprintf(stderr, "Stream failure: %s\n",
				dmsg_msg_str(msg));
		} else {
			write(1, "debug> ", 7);
		}
		break;
	case DMSG_LNK_ERROR | DMSGF_DELETE:
		/* ignore termination of LNK_CONN */
		break;
#endif
	case DMSG_DBG_SHELL:
		/*
		 * We send the commands, not accept them.
		 * (one-way message, not transactional)
		 */
		if (unmanaged)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * A reply from the remote is data we copy to stdout.
		 * (one-way message, not transactional)
		 */
		if (msg->aux_size) {
			msg->aux_data[msg->aux_size - 1] = 0;
			write(1, msg->aux_data, strlen(msg->aux_data));
		}
		break;
#if 1
	case DMSG_LNK_CONN | DMSGF_CREATE:
		fprintf(stderr, "Debug Shell received LNK_CONN\n");
		nmsg = dmsg_msg_alloc(&msg->state->iocom->state0, 0,
				      DMSG_DBG_SHELL,
				      NULL, NULL);
		dmsg_msg_write(nmsg);
		dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_LNK_CONN | DMSGF_DELETE:
		break;
#endif
	default:
		/*
		 * Ignore any unknown messages, Terminate any unknown
		 * transactions with an error.
		 */
		fprintf(stderr, "Unknown message: %s\n", dmsg_msg_str(msg));
		if (unmanaged) {
			if (msg->any.head.cmd & DMSGF_CREATE)
				dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
			if (msg->any.head.cmd & DMSGF_DELETE)
				dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		}
		break;
	}
}

/*
 * Debug session front-end
 */
static
void
shell_ttymsg(dmsg_iocom_t *iocom)
{
	dmsg_state_t *pstate;
	dmsg_msg_t *msg;
	char buf[256];
	char *cmd;
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) != NULL) {
		if (buf[0] == '@') {
			pstate = dmsg_findspan(strtok(buf + 1, " \t\n"));
			cmd = strtok(NULL, "\n");
		} else {
			pstate = &iocom->state0;
			cmd = strtok(buf, "\n");
		}
		if (cmd && pstate) {
			len = strlen(cmd) + 1;
			msg = dmsg_msg_alloc(pstate, len, DMSG_DBG_SHELL,
					     NULL, NULL);
			bcopy(cmd, msg->aux_data, len);
			dmsg_msg_write(msg);
		} else if (cmd) {
			fprintf(stderr, "@msgid not found\n");
		} else {
			/*
			 * This should cause the remote end to generate
			 * a debug> prompt (and thus shows that there is
			 * connectivity).
			 */
			msg = dmsg_msg_alloc(pstate, 0, DMSG_DBG_SHELL,
					     NULL, NULL);
			dmsg_msg_write(msg);
		}
	} else if (feof(stdin)) {
		/*
		 * Set EOF flag without setting any error code for normal
		 * EOF.
		 */
		iocom->flags |= DMSG_IOCOMF_EOF;
	} else {
		clearerr(stdin);
	}
}

/*
 * Debug session back-end (on remote side)
 */
static void shell_span(dmsg_msg_t *msg, char *cmdbuf);
static void shell_ping(dmsg_msg_t *msg, char *cmdbuf);

void
hammer2_shell_parse(dmsg_msg_t *msg, int unmanaged)
{
	dmsg_iocom_t *iocom = msg->state->iocom;
	char *cmdbuf;
	char *cmdp;
	uint32_t cmd;

	/*
	 * Filter on debug shell commands and ping responses only
	 */
	cmd = msg->any.head.cmd;
	if ((cmd & DMSGF_CMDSWMASK) == (DMSG_LNK_PING | DMSGF_REPLY)) {
		dmsg_printf(iocom, "ping reply\n");
		return;
	}

	if ((cmd & DMSGF_PROTOS) != DMSG_PROTO_DBG) {
		if (unmanaged)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		return;
	}
	if ((cmd & DMSGF_CMDSWMASK) != DMSG_DBG_SHELL) {
		if (unmanaged)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		return;
	}

	/*
	 * Debug shell command
	 */
	cmdbuf = msg->aux_data;
	cmdp = strsep(&cmdbuf, " \t");

	if (cmdp == NULL || *cmdp == 0) {
		;
	} else if (strcmp(cmdp, "ping") == 0) {
		shell_ping(msg, cmdbuf);
	} else if (strcmp(cmdp, "span") == 0) {
		shell_span(msg, cmdbuf);
	} else if (strcmp(cmdp, "tree") == 0) {
		dmsg_shell_tree(iocom, cmdbuf); /* dump spanning tree */
	} else if (strcmp(cmdp, "help") == 0 || strcmp(cmdp, "?") == 0) {
		dmsg_printf(iocom, "help            Command help\n");
		dmsg_printf(iocom, "span <host>     Span to target host\n");
		dmsg_printf(iocom, "tree            Dump spanning tree\n");
		dmsg_printf(iocom, "@span <cmd>     Issue via circuit\n");
	} else {
		dmsg_printf(iocom, "Unrecognized command: %s\n", cmdp);
	}
	dmsg_printf(iocom, "debug> ");
}

static void
shell_ping(dmsg_msg_t *msg, char *cmdbuf __unused)
{
	dmsg_iocom_t *iocom = msg->state->iocom;
	dmsg_msg_t *m2;

	dmsg_printf(iocom, "sending ping\n");
	m2 = dmsg_msg_alloc(msg->state, 0, DMSG_LNK_PING, NULL, NULL);
	dmsg_msg_write(m2);
}

static void
shell_span(dmsg_msg_t *msg, char *cmdbuf)
{
	dmsg_iocom_t *iocom = msg->state->iocom;
	dmsg_master_service_info_t *info;
	const char *hostname = strsep(&cmdbuf, " \t");
	pthread_t thread;
	int fd;

	/*
	 * Connect to the target
	 */
	if (hostname == NULL) {
		fd = -1;
	} else {
		fd = dmsg_connect(hostname);
	}

	/*
	 * Start master service
	 */
	if (fd < 0) {
		dmsg_printf(iocom, "Connection to %s failed\n", hostname);
	} else {
		dmsg_printf(iocom, "Connected to %s\n", hostname);

		info = malloc(sizeof(*info));
		bzero(info, sizeof(*info));
		info->fd = fd;
		info->detachme = 1;
		info->usrmsg_callback = hammer2_shell_parse;
		info->label = strdup("client");

		pthread_create(&thread, NULL, dmsg_master_service, info);
		/*pthread_join(thread, &res);*/
	}
}

/************************************************************************
 *				DEBUGSPAN				*
 ************************************************************************
 *
 * Connect to the target manually (not via the cluster list embedded in
 * a hammer2 filesystem) and initiate the SPAN protocol.
 */
int
cmd_debugspan(const char *hostname)
{
	pthread_t thread;
	int fd;
	void *res;

	/*
	 * Connect to the target
	 */
	fd = dmsg_connect(hostname);
	if (fd < 0)
		return 1;

	printf("debugspan: connected to %s, starting CONN/SPAN\n", hostname);
	pthread_create(&thread, NULL,
		       dmsg_master_service, (void *)(intptr_t)fd);
	pthread_join(thread, &res);
	return(0);
}

/************************************************************************
 *				    SHOW				*
 ************************************************************************/

static void show_volhdr(hammer2_volume_data_t *voldata, int fd, int bi);
static void show_bref(hammer2_volume_data_t *voldata, int fd, int tab,
			int bi, hammer2_blockref_t *bref, int norecurse);
static void tabprintf(int tab, const char *ctl, ...);

static hammer2_off_t TotalAccum16[4]; /* includes TotalAccum64 */
static hammer2_off_t TotalAccum64[4];
static hammer2_off_t TotalUnavail;
static hammer2_off_t TotalFreemap;

int
cmd_show(const char *devpath, int which)
{
	hammer2_blockref_t broot;
	hammer2_blockref_t best;
	hammer2_media_data_t media;
	hammer2_media_data_t best_media;
	int fd;
	int i;
	int best_i;
	char *env;

	memset(TotalAccum16, 0, sizeof(TotalAccum16));
	memset(TotalAccum64, 0, sizeof(TotalAccum64));
	TotalUnavail = TotalFreemap = 0;

	env = getenv("HAMMER2_SHOW_TAB");
	if (env != NULL) {
		show_tab = (int)strtol(env, NULL, 0);
		if (errno || show_tab < 0 || show_tab > 8)
			show_tab = 2;
	}
	env = getenv("HAMMER2_SHOW_DEPTH");
	if (env != NULL) {
		show_depth = (int)strtol(env, NULL, 0);
		if (errno || show_depth < 0)
			show_depth = -1;
	}

	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	/*
	 * Show the tree using the best volume header.
	 * -vvv will show the tree for all four volume headers.
	 */
	best_i = -1;
	bzero(&best, sizeof(best));
	bzero(&best_media, sizeof(best_media));
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		bzero(&broot, sizeof(broot));
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) | HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);
		if (read(fd, &media, HAMMER2_PBUFSIZE) ==
		    (ssize_t)HAMMER2_PBUFSIZE) {
			broot.mirror_tid = media.voldata.mirror_tid;
			if (best_i < 0 || best.mirror_tid < broot.mirror_tid) {
				best_i = i;
				best = broot;
				best_media = media;
			}
			printf("Volume header %d: mirror_tid=%016jx\n",
			       i, (intmax_t)broot.mirror_tid);

			if (VerboseOpt >= 3) {
				switch(which) {
				case 0:
					broot.type = HAMMER2_BREF_TYPE_VOLUME;
					show_bref(&media.voldata, fd, 0,
						  i, &broot, 0);
					break;
				case 1:
					broot.type = HAMMER2_BREF_TYPE_FREEMAP;
					show_bref(&media.voldata, fd, 0,
						  i, &broot, 0);
					break;
				default:
					show_volhdr(&media.voldata, fd, i);
					break;
				}
				if (i != HAMMER2_NUM_VOLHDRS - 1)
					printf("\n");
			}
		}
	}
	if (VerboseOpt < 3) {
		switch(which) {
		case 0:
			best.type = HAMMER2_BREF_TYPE_VOLUME;
			show_bref(&best_media.voldata, fd, 0, best_i, &best, 0);
			break;
		case 1:
			best.type = HAMMER2_BREF_TYPE_FREEMAP;
			show_bref(&best_media.voldata, fd, 0, best_i, &best, 0);
			break;
		default:
			show_volhdr(&best_media.voldata, fd, best_i);
			break;
		}
	}
	close(fd);

	if (which == 1 && VerboseOpt < 3) {
		printf("Total unallocated storage:   %6.3fGiB (%6.3fGiB in 64KB chunks)\n",
		       (double)TotalAccum16[0] / GIG,
		       (double)TotalAccum64[0] / GIG);
		printf("Total possibly free storage: %6.3fGiB (%6.3fGiB in 64KB chunks)\n",
		       (double)TotalAccum16[2] / GIG,
		       (double)TotalAccum64[2] / GIG);
		printf("Total allocated storage:     %6.3fGiB (%6.3fGiB in 64KB chunks)\n",
		       (double)TotalAccum16[3] / GIG,
		       (double)TotalAccum64[3] / GIG);
		printf("Total unavailable storage:   %6.3fGiB\n",
		       (double)TotalUnavail / GIG);
		printf("Total freemap storage:       %6.3fGiB\n",
		       (double)TotalFreemap / GIG);
	}

	return 0;
}

static void
show_volhdr(hammer2_volume_data_t *voldata, int fd, int bi)
{
	uint32_t status;
	uint32_t i;
	char *str;
	char *name;

	printf("\nVolume header %d {\n", bi);
	printf("    magic          0x%016jx\n", (intmax_t)voldata->magic);
	printf("    boot_beg       0x%016jx\n", (intmax_t)voldata->boot_beg);
	printf("    boot_end       0x%016jx (%6.2fMB)\n",
	       (intmax_t)voldata->boot_end,
	       (double)(voldata->boot_end - voldata->boot_beg) /
	       (1024.0*1024.0));
	printf("    aux_beg        0x%016jx\n", (intmax_t)voldata->aux_beg);
	printf("    aux_end        0x%016jx (%6.2fMB)\n",
	       (intmax_t)voldata->aux_end,
	       (double)(voldata->aux_end - voldata->aux_beg) /
	       (1024.0*1024.0));
	printf("    volu_size      0x%016jx (%6.2fGiB)\n",
	       (intmax_t)voldata->volu_size,
	       (double)voldata->volu_size / GIG);
	printf("    version        %d\n", voldata->version);
	printf("    flags          0x%08x\n", voldata->flags);
	printf("    copyid         %d\n", voldata->copyid);
	printf("    freemap_vers   %d\n", voldata->freemap_version);
	printf("    peer_type      %d\n", voldata->peer_type);

	str = NULL;
	hammer2_uuid_to_str(&voldata->fsid, &str);
	printf("    fsid           %s\n", str);
	free(str);

	str = NULL;
	name = NULL;
	hammer2_uuid_to_str(&voldata->fstype, &str);
	printf("    fstype         %s\n", str);
	uuid_addr_lookup(&voldata->fstype, &name, &status);
	if (name == NULL)
		name = strdup("?");
	printf("                   (%s)\n", name);
	free(name);
	free(str);

	printf("    allocator_size 0x%016jx (%6.2fGiB)\n",
	       voldata->allocator_size,
	       (double)voldata->allocator_size / GIG);
	printf("    allocator_free 0x%016jx (%6.2fGiB)\n",
	       voldata->allocator_free,
	       (double)voldata->allocator_free / GIG);
	printf("    allocator_beg  0x%016jx (%6.2fGiB)\n",
	       voldata->allocator_beg,
	       (double)voldata->allocator_beg / GIG);

	printf("    mirror_tid     0x%016jx\n", voldata->mirror_tid);
	printf("    reserved0080   0x%016jx\n", voldata->reserved0080);
	printf("    reserved0088   0x%016jx\n", voldata->reserved0088);
	printf("    freemap_tid    0x%016jx\n", voldata->freemap_tid);
	for (i = 0; i < nitems(voldata->reserved00A0); ++i) {
		printf("    reserved00A0/%u 0x%016jx\n",
		       i, voldata->reserved00A0[0]);
	}

	printf("    copyexists    ");
	for (i = 0; i < nitems(voldata->copyexists); ++i)
		printf(" 0x%02x", voldata->copyexists[i]);
	printf("\n");

	/*
	 * NOTE: Index numbers and ICRC_SECTn definitions are not matched,
	 *	 the ICRC for sector 0 actually uses the last index, for
	 *	 example.
	 *
	 * NOTE: The whole voldata CRC does not have to match critically
	 *	 as certain sub-areas of the volume header have their own
	 *	 CRCs.
	 */
	printf("\n");
	for (i = 0; i < nitems(voldata->icrc_sects); ++i) {
		printf("    icrc_sects[%u]  ", i);
		switch(i) {
		case HAMMER2_VOL_ICRC_SECT0:
			printf("0x%08x/0x%08x",
			       hammer2_icrc32((char *)voldata +
					      HAMMER2_VOLUME_ICRC0_OFF,
					      HAMMER2_VOLUME_ICRC0_SIZE),
			       voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0]);
			if (hammer2_icrc32((char *)voldata +
					   HAMMER2_VOLUME_ICRC0_OFF,
					   HAMMER2_VOLUME_ICRC0_SIZE) ==
			       voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT0]) {
				printf(" (OK)");
			} else {
				printf(" (FAILED)");
			}
			break;
		case HAMMER2_VOL_ICRC_SECT1:
			printf("0x%08x/0x%08x",
			       hammer2_icrc32((char *)voldata +
					      HAMMER2_VOLUME_ICRC1_OFF,
					      HAMMER2_VOLUME_ICRC1_SIZE),
			       voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1]);
			if (hammer2_icrc32((char *)voldata +
					   HAMMER2_VOLUME_ICRC1_OFF,
					   HAMMER2_VOLUME_ICRC1_SIZE) ==
			       voldata->icrc_sects[HAMMER2_VOL_ICRC_SECT1]) {
				printf(" (OK)");
			} else {
				printf(" (FAILED)");
			}

			break;
		default:
			printf("0x%08x (reserved)", voldata->icrc_sects[i]);
			break;
		}
		printf("\n");
	}
	printf("    icrc_volhdr    0x%08x/0x%08x",
	       hammer2_icrc32((char *)voldata + HAMMER2_VOLUME_ICRCVH_OFF,
			      HAMMER2_VOLUME_ICRCVH_SIZE),
	       voldata->icrc_volheader);
	if (hammer2_icrc32((char *)voldata + HAMMER2_VOLUME_ICRCVH_OFF,
			   HAMMER2_VOLUME_ICRCVH_SIZE) ==
	    voldata->icrc_volheader) {
		printf(" (OK)\n");
	} else {
		printf(" (FAILED - not a critical error)\n");
	}

	/*
	 * The super-root and freemap blocksets (not recursed)
	 */
	printf("\n");
	printf("    sroot_blockset {\n");
	for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
		show_bref(voldata, fd, 16, i,
			  &voldata->sroot_blockset.blockref[i], 2);
	}
	printf("    }\n");

	printf("    freemap_blockset {\n");
	for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
		show_bref(voldata, fd, 16, i,
			  &voldata->freemap_blockset.blockref[i], 2);
	}
	printf("    }\n");

	printf("}\n");
}

static void
show_bref(hammer2_volume_data_t *voldata, int fd, int tab,
	  int bi, hammer2_blockref_t *bref, int norecurse)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	hammer2_off_t tmp;
	int i, bcount, namelen, failed, obrace;
	int type_pad;
	size_t bytes;
	const char *type_str;
	char *str = NULL;
	uint32_t cv;
	uint64_t cv64;
	static int init_tab = -1;

	SHA256_CTX hash_ctx;
	union {
		uint8_t digest[SHA256_DIGEST_LENGTH];
		uint64_t digest64[SHA256_DIGEST_LENGTH/8];
	} u;

	if (init_tab == -1)
		init_tab = tab;

	bytes = (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes)
		bytes = (size_t)1 << bytes;
	if (bytes) {
		hammer2_off_t io_off;
		hammer2_off_t io_base;
		size_t io_bytes;
		size_t boff;

		io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
		io_base = io_off & ~(hammer2_off_t)(HAMMER2_LBUFSIZE - 1);
		boff = io_off - io_base;

		io_bytes = HAMMER2_LBUFSIZE;
		while (io_bytes + boff < bytes)
			io_bytes <<= 1;

		if (io_bytes > sizeof(media)) {
			printf("(bad block size %zu)\n", bytes);
			return;
		}
		if (bref->type != HAMMER2_BREF_TYPE_DATA || VerboseOpt >= 1) {
			lseek(fd, io_base, SEEK_SET);
			if (read(fd, &media, io_bytes) != (ssize_t)io_bytes) {
				printf("(media read failed)\n");
				return;
			}
			if (boff)
				bcopy((char *)&media + boff, &media, bytes);
		}
	}

	bscan = NULL;
	bcount = 0;
	namelen = 0;
	failed = 0;
	obrace = 1;

	type_str = hammer2_breftype_to_str(bref->type);
	type_pad = 8 - strlen(type_str);
	if (type_pad < 0)
		type_pad = 0;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		assert(bytes);
		if (!(media.ipdata.meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
			bscan = &media.ipdata.u.blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		assert(bytes);
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bscan = &media.voldata.sroot_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		bscan = &media.voldata.freemap_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		assert(bytes);
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	}

	if (QuietOpt > 0) {
		tabprintf(tab,
			  "%s.%-3d %016jx %016jx/%-2d "
			  "mir=%016jx mod=%016jx leafcnt=%d ",
			  type_str, bi, (intmax_t)bref->data_off,
			  (intmax_t)bref->key, (intmax_t)bref->keybits,
			  (intmax_t)bref->mirror_tid,
			  (intmax_t)bref->modify_tid,
			  bref->leaf_count);
	} else {
		tabprintf(tab, "%s.%-3d%*.*s 0x%016jx 0x%016jx/%-2d ",
			  type_str, bi, type_pad, type_pad, "",
			  (intmax_t)bref->data_off,
			  (intmax_t)bref->key, (intmax_t)bref->keybits);
		/*if (norecurse > 1)*/ {
			printf("\n");
			tabprintf(tab + 13, "");
		}
		printf("mir=%016jx mod=%016jx lfcnt=%d ",
		       (intmax_t)bref->mirror_tid, (intmax_t)bref->modify_tid,
		       bref->leaf_count);
		if (/*norecurse > 1 && */ (bcount || bref->flags ||
		    bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
		    bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF)) {
			printf("\n");
			tabprintf(tab + 13, "");
		}
	}

	if (bcount)
		printf("bcnt=%d ", bcount);
	if (bref->flags)
		printf("flags=%02x ", bref->flags);
	if (bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		printf("bigmask=%08x avail=%ld ",
			bref->check.freemap.bigmask, bref->check.freemap.avail);
	}

	/*
	 * Check data integrity in verbose mode, otherwise we are just doing
	 * a quick meta-data scan.  Meta-data integrity is always checked.
	 * (Also see the check above that ensures the media data is loaded,
	 * otherwise there's no data to check!).
	 *
	 * WARNING! bref->check state may be used for other things when
	 *	    bref has no data (bytes == 0).
	 */
	if (bytes &&
	    (bref->type != HAMMER2_BREF_TYPE_DATA || VerboseOpt >= 1)) {
		if (!(QuietOpt > 0)) {
			/*if (norecurse > 1)*/ {
				printf("\n");
				tabprintf(tab + 13, "");
			}
		}

		switch(HAMMER2_DEC_CHECK(bref->methods)) {
		case HAMMER2_CHECK_NONE:
			printf("meth=%02x ", bref->methods);
			break;
		case HAMMER2_CHECK_DISABLED:
			printf("meth=%02x ", bref->methods);
			break;
		case HAMMER2_CHECK_ISCSI32:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.iscsi32.value != cv) {
				printf("(icrc %02x:%08x/%08x failed) ",
				       bref->methods,
				       bref->check.iscsi32.value,
				       cv);
				failed = 1;
			} else {
				printf("meth=%02x iscsi32=%08x ",
				       bref->methods, cv);
			}
			break;
		case HAMMER2_CHECK_XXHASH64:
			cv64 = XXH64(&media, bytes, XXH_HAMMER2_SEED);
			if (bref->check.xxhash64.value != cv64) {
				printf("(xxhash64 %02x:%016jx/%016jx failed) ",
				       bref->methods,
				       bref->check.xxhash64.value,
				       cv64);
				failed = 1;
			} else {
				printf("meth=%02x xxh=%016jx ",
				       bref->methods, cv64);
			}
			break;
		case HAMMER2_CHECK_SHA192:
			SHA256_Init(&hash_ctx);
			SHA256_Update(&hash_ctx, &media, bytes);
			SHA256_Final(u.digest, &hash_ctx);
			u.digest64[2] ^= u.digest64[3];
			if (memcmp(u.digest, bref->check.sha192.data,
			    sizeof(bref->check.sha192.data))) {
				printf("(sha192 failed) ");
				failed = 1;
			} else {
				printf("meth=%02x ", bref->methods);
			}
			break;
		case HAMMER2_CHECK_FREEMAP:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.freemap.icrc32 != cv) {
				printf("(fcrc %02x:%08x/%08x failed) ",
					bref->methods,
					bref->check.freemap.icrc32,
					cv);
				failed = 1;
			} else {
				printf("meth=%02x fcrc=%08x ",
					bref->methods, cv);
			}
			break;
		}
	}

	tab += show_tab;

	if (QuietOpt > 0) {
		obrace = 0;
		printf("\n");
		goto skip_data;
	}

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_EMPTY:
		if (norecurse)
			printf("\n");
		obrace = 0;
		break;
	case HAMMER2_BREF_TYPE_DIRENT:
		printf("{\n");
		if (bref->embed.dirent.namlen <= sizeof(bref->check.buf)) {
			tabprintf(tab, "filename \"%*.*s\"\n",
				bref->embed.dirent.namlen,
				bref->embed.dirent.namlen,
				bref->check.buf);
		} else {
			tabprintf(tab, "filename \"%*.*s\"\n",
				bref->embed.dirent.namlen,
				bref->embed.dirent.namlen,
				media.buf);
		}
		tabprintf(tab, "inum 0x%016jx\n",
			  (uintmax_t)bref->embed.dirent.inum);
		tabprintf(tab, "nlen %d\n", bref->embed.dirent.namlen);
		tabprintf(tab, "type %s\n",
			  hammer2_iptype_to_str(bref->embed.dirent.type));
		break;
	case HAMMER2_BREF_TYPE_INODE:
		printf("{\n");
		namelen = media.ipdata.meta.name_len;
		if (namelen > HAMMER2_INODE_MAXNAME)
			namelen = 0;
		tabprintf(tab, "filename \"%*.*s\"\n",
			  namelen, namelen, media.ipdata.filename);
		tabprintf(tab, "version  %d\n", media.ipdata.meta.version);
		tabprintf(tab, "pfs_st   %d\n", media.ipdata.meta.pfs_subtype);
		tabprintf(tab, "uflags   0x%08x\n",
			  media.ipdata.meta.uflags);
		if (media.ipdata.meta.rmajor || media.ipdata.meta.rminor) {
			tabprintf(tab, "rmajor   %d\n",
				  media.ipdata.meta.rmajor);
			tabprintf(tab, "rminor   %d\n",
				  media.ipdata.meta.rminor);
		}
		tabprintf(tab, "ctime    %s\n",
			  hammer2_time64_to_str(media.ipdata.meta.ctime, &str));
		tabprintf(tab, "mtime    %s\n",
			  hammer2_time64_to_str(media.ipdata.meta.mtime, &str));
		tabprintf(tab, "atime    %s\n",
			  hammer2_time64_to_str(media.ipdata.meta.atime, &str));
		tabprintf(tab, "btime    %s\n",
			  hammer2_time64_to_str(media.ipdata.meta.btime, &str));
		tabprintf(tab, "uid      %s\n",
			  hammer2_uuid_to_str(&media.ipdata.meta.uid, &str));
		tabprintf(tab, "gid      %s\n",
			  hammer2_uuid_to_str(&media.ipdata.meta.gid, &str));
		tabprintf(tab, "type     %s\n",
			  hammer2_iptype_to_str(media.ipdata.meta.type));
		tabprintf(tab, "opflgs   0x%02x\n",
			  media.ipdata.meta.op_flags);
		tabprintf(tab, "capflgs  0x%04x\n",
			  media.ipdata.meta.cap_flags);
		tabprintf(tab, "mode     %-7o\n",
			  media.ipdata.meta.mode);
		tabprintf(tab, "inum     0x%016jx\n",
			  media.ipdata.meta.inum);
		tabprintf(tab, "size     %ju ",
			  (uintmax_t)media.ipdata.meta.size);
		if (media.ipdata.meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA &&
		    media.ipdata.meta.size <= HAMMER2_EMBEDDED_BYTES)
			printf("(embedded data)\n");
		else
			printf("\n");
		tabprintf(tab, "nlinks   %ju\n",
			  (uintmax_t)media.ipdata.meta.nlinks);
		tabprintf(tab, "iparent  0x%016jx\n",
			  (uintmax_t)media.ipdata.meta.iparent);
		tabprintf(tab, "name_key 0x%016jx\n",
			  (uintmax_t)media.ipdata.meta.name_key);
		tabprintf(tab, "name_len %u\n",
			  media.ipdata.meta.name_len);
		tabprintf(tab, "ncopies  %u\n",
			  media.ipdata.meta.ncopies);
		tabprintf(tab, "compalg  %u\n",
			  media.ipdata.meta.comp_algo);
		tabprintf(tab, "target_t %u\n",
			  media.ipdata.meta.target_type);
		tabprintf(tab, "checkalg %u\n",
			  media.ipdata.meta.check_algo);
		if ((media.ipdata.meta.op_flags & HAMMER2_OPFLAG_PFSROOT) ||
		    media.ipdata.meta.pfs_type == HAMMER2_PFSTYPE_SUPROOT) {
			tabprintf(tab, "pfs_nmas %u\n",
				  media.ipdata.meta.pfs_nmasters);
			tabprintf(tab, "pfs_type %u (%s)\n",
				  media.ipdata.meta.pfs_type,
				  hammer2_pfstype_to_str(media.ipdata.meta.pfs_type));
			tabprintf(tab, "pfs_inum 0x%016jx\n",
				  (uintmax_t)media.ipdata.meta.pfs_inum);
			tabprintf(tab, "pfs_clid %s\n",
				  hammer2_uuid_to_str(&media.ipdata.meta.pfs_clid,
						      &str));
			tabprintf(tab, "pfs_fsid %s\n",
				  hammer2_uuid_to_str(&media.ipdata.meta.pfs_fsid,
						      &str));
			tabprintf(tab, "pfs_lsnap_tid 0x%016jx\n",
				  (uintmax_t)media.ipdata.meta.pfs_lsnap_tid);
		}
		tabprintf(tab, "data_quota  %ju\n",
			  (uintmax_t)media.ipdata.meta.data_quota);
		tabprintf(tab, "data_count  %ju\n",
			  (uintmax_t)bref->embed.stats.data_count);
		tabprintf(tab, "inode_quota %ju\n",
			  (uintmax_t)media.ipdata.meta.inode_quota);
		tabprintf(tab, "inode_count %ju\n",
			  (uintmax_t)bref->embed.stats.inode_count);
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		printf("{\n");
		break;
	case HAMMER2_BREF_TYPE_DATA:
		printf("\n");
		obrace = 0;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		printf("mirror_tid=%016jx freemap_tid=%016jx ",
			media.voldata.mirror_tid,
			media.voldata.freemap_tid);
		printf("{\n");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		printf("mirror_tid=%016jx freemap_tid=%016jx ",
			media.voldata.mirror_tid,
			media.voldata.freemap_tid);
		printf("{\n");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		printf("{\n");
		tmp = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
		tmp &= HAMMER2_SEGMASK;
		tmp /= HAMMER2_PBUFSIZE;
		assert(tmp >= HAMMER2_ZONE_FREEMAP_00);
		assert(tmp < HAMMER2_ZONE_FREEMAP_END);
		tmp -= HAMMER2_ZONE_FREEMAP_00;
		tmp /= HAMMER2_ZONE_FREEMAP_INC;
		tabprintf(tab, "rotation=%d\n", (int)tmp);

		for (i = 0; i < HAMMER2_FREEMAP_COUNT; ++i) {
			hammer2_off_t data_off = bref->key +
				i * HAMMER2_FREEMAP_LEVEL0_SIZE;
#if HAMMER2_BMAP_ELEMENTS != 8
#error "cmd_debug.c: HAMMER2_BMAP_ELEMENTS expected to be 8"
#endif
			tabprintf(tab + 4, "%016jx %04d.%04x (avail=%7d) "
				  "%016jx %016jx %016jx %016jx "
				  "%016jx %016jx %016jx %016jx\n",
				  data_off, i, media.bmdata[i].class,
				  media.bmdata[i].avail,
				  media.bmdata[i].bitmapq[0],
				  media.bmdata[i].bitmapq[1],
				  media.bmdata[i].bitmapq[2],
				  media.bmdata[i].bitmapq[3],
				  media.bmdata[i].bitmapq[4],
				  media.bmdata[i].bitmapq[5],
				  media.bmdata[i].bitmapq[6],
				  media.bmdata[i].bitmapq[7]);
		}
		tabprintf(tab, "}\n");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		printf("{\n");
		tmp = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
		tmp &= HAMMER2_SEGMASK;
		tmp /= HAMMER2_PBUFSIZE;
		assert(tmp >= HAMMER2_ZONE_FREEMAP_00);
		assert(tmp < HAMMER2_ZONE_FREEMAP_END);
		tmp -= HAMMER2_ZONE_FREEMAP_00;
		tmp /= HAMMER2_ZONE_FREEMAP_INC;
		tabprintf(tab, "rotation=%d\n", (int)tmp);
		break;
	default:
		printf("\n");
		obrace = 0;
		break;
	}
	if (str)
		free(str);

skip_data:
	/*
	 * Update statistics.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		for (i = 0; i < HAMMER2_FREEMAP_COUNT; ++i) {
			hammer2_off_t data_off = bref->key +
				i * HAMMER2_FREEMAP_LEVEL0_SIZE;
			if (data_off >= voldata->aux_end &&
			    data_off < voldata->volu_size) {
				int j;
				for (j = 0; j < 4; ++j)
					CountBlocks(&media.bmdata[i], j,
						    &TotalAccum16[j],
						    &TotalAccum64[j]);
			} else
				TotalUnavail += HAMMER2_FREEMAP_LEVEL0_SIZE;
		}
		TotalFreemap += HAMMER2_FREEMAP_LEVEL1_SIZE;
		break;
	default:
		break;
	}

	/*
	 * Recurse if norecurse == 0.  If the CRC failed, pass norecurse = 1.
	 * That is, if an indirect or inode fails we still try to list its
	 * direct children to help with debugging, but go no further than
	 * that because they are probably garbage.
	 */
	if (show_depth == -1 || ((tab - init_tab) / show_tab) < show_depth) {
		for (i = 0; norecurse == 0 && i < bcount; ++i) {
			if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY) {
				show_bref(voldata, fd, tab, i, &bscan[i],
				    failed);
			}
		}
	}
	tab -= show_tab;
	if (obrace) {
		if (bref->type == HAMMER2_BREF_TYPE_INODE)
			tabprintf(tab, "} (%s.%d, \"%*.*s\")\n",
				  type_str, bi, namelen, namelen,
				  media.ipdata.filename);
		else
			tabprintf(tab, "} (%s.%d)\n", type_str, bi);
	}
}

static
void
CountBlocks(hammer2_bmap_data_t *bmap, int value,
	    hammer2_off_t *accum16, hammer2_off_t *accum64)
{
	int i, j, bits;
	hammer2_bitmap_t value16, value64;

	bits = (int)sizeof(hammer2_bitmap_t) * 8;
	assert(bits == 64);

	value16 = value;
	assert(value16 < 4);
	value64 = (value16 << 6) | (value16 << 4) | (value16 << 2) | value16;
	assert(value64 < 256);

	for (i = 0; i < HAMMER2_BMAP_ELEMENTS; ++i) {
		hammer2_bitmap_t bm = bmap->bitmapq[i];
		hammer2_bitmap_t bm_save = bm;
		hammer2_bitmap_t mask;

		mask = 0x03; /* 2 bits per 16KB */
		for (j = 0; j < bits; j += 2) {
			if ((bm & mask) == value16)
				*accum16 += 16384;
			bm >>= 2;
		}

		bm = bm_save;
		mask = 0xFF; /* 8 bits per 64KB chunk */
		for (j = 0; j < bits; j += 8) {
			if ((bm & mask) == value64)
				*accum64 += 65536;
			bm >>= 8;
		}
	}
}

int
cmd_hash(int ac, const char **av)
{
	int i;

	for (i = 0; i < ac; ++i) {
		printf("%016jx %s\n",
		       dirhash((const unsigned char*)av[i], strlen(av[i])),
		       av[i]);
	}
	return(0);
}

int
cmd_dhash(int ac, const char **av)
{
	char buf[1024];		/* 1K extended directory record */
	uint64_t hash;
	int i;

	for (i = 0; i < ac; ++i) {
		bzero(buf, sizeof(buf));
		snprintf(buf, sizeof(buf), "%s", av[i]);
		hash = XXH64(buf, sizeof(buf), XXH_HAMMER2_SEED);
		printf("%016jx %s\n", hash, av[i]);
	}
	return(0);
}

int
cmd_dumpchain(const char *path, u_int flags)
{
	int dummy = (int)flags;
	int ecode = 0;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		if (ioctl(fd, HAMMER2IOC_DEBUG_DUMP, &dummy) < 0) {
			fprintf(stderr, "%s: %s\n", path, strerror(errno));
			ecode = 1;
		}
		close(fd);
	} else {
		fprintf(stderr, "unable to open %s\n", path);
		ecode = 1;
	}
	return ecode;
}

static
void
tabprintf(int tab, const char *ctl, ...)
{
	va_list va;

	printf("%*.*s", tab, tab, "");
	va_start(va, ctl);
	vprintf(ctl, va);
	va_end(va);
}
