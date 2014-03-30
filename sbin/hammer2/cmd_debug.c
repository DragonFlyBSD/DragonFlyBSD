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

#define SHOW_TAB	2

static void shell_rcvmsg(dmsg_msg_t *msg);
static void shell_ttymsg(dmsg_iocom_t *iocom);

/************************************************************************
 *				    SHELL				*
 ************************************************************************/

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
	 * Run the session.  The remote end transmits our prompt.
	 */
	dmsg_iocom_init(&iocom, fd, 0,
			NULL,
			shell_rcvmsg,
			hammer2_shell_parse,
			shell_ttymsg);
	fcntl(0, F_SETFL, O_NONBLOCK);
	printf("debug: connected\n");

	msg = dmsg_msg_alloc(&iocom.circuit0, 0, DMSG_DBG_SHELL, NULL, NULL);
	dmsg_msg_write(msg);
	dmsg_iocom_core(&iocom);
	fprintf(stderr, "debug: disconnected\n");
	close(fd);
	return 0;
}

/*
 * Callback from dmsg_iocom_core() when messages might be present
 * on the socket.
 */
static
void
shell_rcvmsg(dmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
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
	case DMSG_DBG_SHELL:
		/*
		 * We send the commands, not accept them.
		 * (one-way message, not transactional)
		 */
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
	case DMSG_LNK_CONN | DMSGF_CREATE:
		fprintf(stderr, "Debug Shell is ignoring received LNK_CONN\n");
		dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_LNK_CONN | DMSGF_DELETE:
		break;
	default:
		/*
		 * Ignore any unknown messages, Terminate any unknown
		 * transactions with an error.
		 */
		fprintf(stderr, "Unknown message: %s\n", dmsg_msg_str(msg));
		if (msg->any.head.cmd & DMSGF_CREATE)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		if (msg->any.head.cmd & DMSGF_DELETE)
			dmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
}

static
void
shell_ttymsg(dmsg_iocom_t *iocom)
{
	dmsg_msg_t *msg;
	char buf[256];
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) != NULL) {
		len = strlen(buf);
		if (len && buf[len - 1] == '\n')
			buf[--len] = 0;
		++len;
		msg = dmsg_msg_alloc(&iocom->circuit0, len, DMSG_DBG_SHELL,
				     NULL, NULL);
		bcopy(buf, msg->aux_data, len);
		dmsg_msg_write(msg);
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

static void shell_span(dmsg_circuit_t *circuit, char *cmdbuf);
static void shell_circ(dmsg_circuit_t *circuit, char *cmdbuf);

void
hammer2_shell_parse(dmsg_msg_t *msg)
{
	dmsg_circuit_t *circuit = msg->circuit;
	char *cmdbuf = msg->aux_data;
	char *cmd = strsep(&cmdbuf, " \t");

	if (cmd == NULL || *cmd == 0) {
		;
	} else if (strcmp(cmd, "span") == 0) {
		shell_span(circuit, cmdbuf);
	} else if (strcmp(cmd, "circ") == 0) {
		shell_circ(circuit, cmdbuf);
	} else if (strcmp(cmd, "tree") == 0) {
		dmsg_shell_tree(circuit, cmdbuf); /* dump spanning tree */
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		dmsg_circuit_printf(circuit, "help            Command help\n");
		dmsg_circuit_printf(circuit, "span <host>     Span to target host\n");
		dmsg_circuit_printf(circuit, "circ <msgid>    Create VC to msgid of rx SPAN\n");
		dmsg_circuit_printf(circuit, "tree            Dump spanning tree\n");
	} else {
		dmsg_circuit_printf(circuit, "Unrecognized command: %s\n", cmd);
	}
}

static void
shell_span(dmsg_circuit_t *circuit, char *cmdbuf)
{
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
		dmsg_circuit_printf(circuit,
				    "Connection to %s failed\n",
				    hostname);
	} else {
		dmsg_circuit_printf(circuit, "Connected to %s\n", hostname);

		info = malloc(sizeof(*info));
		bzero(info, sizeof(*info));
		info->fd = fd;
		info->detachme = 1;
		info->dbgmsg_callback = hammer2_shell_parse;
		info->label = strdup("client");

		pthread_create(&thread, NULL, dmsg_master_service, info);
		/*pthread_join(thread, &res);*/
	}
}

static void shell_circ_reply(dmsg_msg_t *msg);

static void
shell_circ(dmsg_circuit_t *circuit, char *cmdbuf)
{
	uint64_t msgid = strtoull(cmdbuf, NULL, 16);
	dmsg_state_t *state;
	dmsg_msg_t *msg;

	if (dmsg_debug_findspan(msgid, &state) == 0) {
		dmsg_circuit_printf(circuit, "Found state %p\n", state);

		dmsg_circuit_printf(circuit, "Establishing CIRC\n");
		msg = dmsg_msg_alloc(&state->iocom->circuit0, 0,
				     DMSG_LNK_CIRC | DMSGF_CREATE,
				     shell_circ_reply, circuit);
		msg->any.lnk_circ.target = state->msgid;
		dmsg_msg_write(msg);
	} else {
		dmsg_circuit_printf(circuit,
				    "Unable to locate %016jx\n",
				    (intmax_t)msgid);
	}
}

static void
shell_circ_reply(dmsg_msg_t *msg)
{
	dmsg_circuit_t *circ = msg->state->any.circ;

	if (msg->any.head.cmd & DMSGF_DELETE) {
		dmsg_circuit_printf(circ, "rxmsg DELETE error %d\n",
				    msg->any.head.error);
		msg->state->any.circ = NULL;
	} else {
		dmsg_circuit_printf(circ, "rxmsg result error %d\n",
				    msg->any.head.error);
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

static void show_bref(int fd, int tab, int bi, hammer2_blockref_t *bref,
			int dofreemap);
static void tabprintf(int tab, const char *ctl, ...);

int
cmd_show(const char *devpath, int dofreemap)
{
	hammer2_blockref_t broot;
	hammer2_blockref_t best;
	hammer2_media_data_t media;
	int fd;
	int i;
	int best_i;

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
	for (i = 0; i < 4; ++i) {
		bzero(&broot, sizeof(broot));
		broot.type = HAMMER2_BREF_TYPE_VOLUME;
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) |
				 HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, 0);
		if (read(fd, &media, HAMMER2_PBUFSIZE) ==
		    (ssize_t)HAMMER2_PBUFSIZE) {
			broot.mirror_tid = media.voldata.mirror_tid;
			if (best_i < 0 || best.mirror_tid < broot.mirror_tid) {
				best_i = i;
				best = broot;
			}
			if (VerboseOpt >= 3)
				show_bref(fd, 0, i, &broot, dofreemap);
		}
	}
	if (VerboseOpt < 3)
		show_bref(fd, 0, best_i, &best, dofreemap);
	close(fd);

	return 0;
}

static void
show_bref(int fd, int tab, int bi, hammer2_blockref_t *bref, int dofreemap)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int bcount;
	int i;
	int didnl;
	int namelen;
	int obrace = 1;
	size_t bytes;
	const char *type_str;
	char *str = NULL;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_EMPTY:
		type_str = "empty";
		break;
	case HAMMER2_BREF_TYPE_INODE:
		type_str = "inode";
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		type_str = "indblk";
		break;
	case HAMMER2_BREF_TYPE_DATA:
		type_str = "data";
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		type_str = "volume";
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		type_str = "freemap";
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		type_str = "fmapnode";
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		type_str = "fbitmap";
		break;
	default:
		type_str = "unknown";
		break;
	}

	tabprintf(tab, "%s.%-3d %016jx %016jx/%-2d mir=%016jx mod=%016jx ",
	       type_str, bi, (intmax_t)bref->data_off,
	       (intmax_t)bref->key, (intmax_t)bref->keybits,
	       (intmax_t)bref->mirror_tid, (intmax_t)bref->modify_tid);
	tab += SHOW_TAB;

	bytes = (size_t)1 << (bref->data_off & HAMMER2_OFF_MASK_RADIX);

	{
		hammer2_off_t io_off;
		hammer2_off_t io_base;
		size_t io_bytes;
		size_t boff;

		io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
		io_base = io_off & ~(hammer2_off_t)(HAMMER2_MINIOSIZE - 1);
		io_bytes = bytes;
		boff = io_off - io_base;

		io_bytes = HAMMER2_MINIOSIZE;
		while (io_bytes + boff < bytes)
			io_bytes <<= 1;

		if (io_bytes > sizeof(media)) {
			printf("(bad block size %zd)\n", bytes);
			return;
		}
		if (bref->type != HAMMER2_BREF_TYPE_DATA || VerboseOpt >= 1) {
			lseek(fd, io_base, 0);
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
	didnl = 1;
	namelen = 0;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_EMPTY:
		obrace = 0;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		printf("{\n");
		if (media.ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
			/* no blockrefs */
		} else {
			bscan = &media.ipdata.u.blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		}
		namelen = media.ipdata.name_len;
		if (namelen > HAMMER2_INODE_MAXNAME)
			namelen = 0;
		tabprintf(tab, "filename \"%*.*s\"\n",
			  namelen, namelen, media.ipdata.filename);
		tabprintf(tab, "version  %d\n", media.ipdata.version);
		tabprintf(tab, "uflags   0x%08x\n",
			  media.ipdata.uflags);
		if (media.ipdata.rmajor || media.ipdata.rminor) {
			tabprintf(tab, "rmajor   %d\n",
				  media.ipdata.rmajor);
			tabprintf(tab, "rminor   %d\n",
				  media.ipdata.rminor);
		}
		tabprintf(tab, "ctime    %s\n",
			  hammer2_time64_to_str(media.ipdata.ctime, &str));
		tabprintf(tab, "mtime    %s\n",
			  hammer2_time64_to_str(media.ipdata.mtime, &str));
		tabprintf(tab, "atime    %s\n",
			  hammer2_time64_to_str(media.ipdata.atime, &str));
		tabprintf(tab, "btime    %s\n",
			  hammer2_time64_to_str(media.ipdata.btime, &str));
		tabprintf(tab, "uid      %s\n",
			  hammer2_uuid_to_str(&media.ipdata.uid, &str));
		tabprintf(tab, "gid      %s\n",
			  hammer2_uuid_to_str(&media.ipdata.gid, &str));
		if (media.ipdata.type == HAMMER2_OBJTYPE_HARDLINK)
			tabprintf(tab, "type     %s (%s)\n",
			      hammer2_iptype_to_str(media.ipdata.type),
			      hammer2_iptype_to_str(media.ipdata.target_type));
		else
			tabprintf(tab, "type     %s\n",
				  hammer2_iptype_to_str(media.ipdata.type));
		tabprintf(tab, "opflgs   0x%02x\n",
			  media.ipdata.op_flags);
		tabprintf(tab, "capflgs  0x%04x\n",
			  media.ipdata.cap_flags);
		tabprintf(tab, "mode     %-7o\n",
			  media.ipdata.mode);
		tabprintf(tab, "inum     0x%016jx\n",
			  media.ipdata.inum);
		tabprintf(tab, "size     %ju\n",
			  (uintmax_t)media.ipdata.size);
		tabprintf(tab, "nlinks   %ju\n",
			  (uintmax_t)media.ipdata.nlinks);
		tabprintf(tab, "iparent  0x%016jx\n",
			  (uintmax_t)media.ipdata.iparent);
		tabprintf(tab, "name_key 0x%016jx\n",
			  (uintmax_t)media.ipdata.name_key);
		tabprintf(tab, "name_len %u\n",
			  media.ipdata.name_len);
		tabprintf(tab, "ncopies  %u\n",
			  media.ipdata.ncopies);
		tabprintf(tab, "compalg  %u\n",
			  media.ipdata.comp_algo);
		if (media.ipdata.op_flags & HAMMER2_OPFLAG_PFSROOT) {
			tabprintf(tab, "pfs_type %u (%s)\n",
				  media.ipdata.pfs_type,
				  hammer2_pfstype_to_str(media.ipdata.pfs_type));
			tabprintf(tab, "pfs_inum 0x%016jx\n",
				  (uintmax_t)media.ipdata.pfs_inum);
			tabprintf(tab, "pfs_clid %s\n",
				  hammer2_uuid_to_str(&media.ipdata.pfs_clid,
						      &str));
			tabprintf(tab, "pfs_fsid %s\n",
				  hammer2_uuid_to_str(&media.ipdata.pfs_fsid,
						      &str));
		}
		tabprintf(tab, "data_quota  %ju\n",
			  (uintmax_t)media.ipdata.data_quota);
		tabprintf(tab, "data_count  %ju\n",
			  (uintmax_t)media.ipdata.data_count);
		tabprintf(tab, "inode_quota %ju\n",
			  (uintmax_t)media.ipdata.inode_quota);
		tabprintf(tab, "inode_count %ju\n",
			  (uintmax_t)media.ipdata.inode_count);
		tabprintf(tab, "attr_tid    0x%016jx\n",
			  (uintmax_t)media.ipdata.attr_tid);
		if (media.ipdata.type == HAMMER2_OBJTYPE_DIRECTORY) {
			tabprintf(tab, "dirent_tid  %016jx\n",
				  (uintmax_t)media.ipdata.dirent_tid);
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		didnl = 1;
		printf("{\n");
		break;
	case HAMMER2_BREF_TYPE_DATA:
		if (VerboseOpt >= 2) {
			printf("{\n");
		} else {
			printf("\n");
			obrace = 0;
		}
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		printf("mirror_tid=%016jx freemap_tid=%016jx ",
			media.voldata.mirror_tid,
			media.voldata.freemap_tid);
		if (dofreemap) {
			bscan = &media.voldata.freemap_blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		} else {
			bscan = &media.voldata.sroot_blockset.blockref[0];
			bcount = HAMMER2_SET_COUNT;
		}
		printf("{\n");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		printf("{\n");
		for (i = 0; i < HAMMER2_FREEMAP_COUNT; ++i) {
			if (media.bmdata[i].class == 0 &&
			    media.bmdata[i].avail == 0) {
				continue;
			}
			tabprintf(tab + 4, "%04d.%04x (avail=%5d) "
				  "%08x %08x %08x %08x %08x %08x %08x %08x\n",
				  i, media.bmdata[i].class,
				  media.bmdata[i].avail,
				  media.bmdata[i].bitmap[0],
				  media.bmdata[i].bitmap[1],
				  media.bmdata[i].bitmap[2],
				  media.bmdata[i].bitmap[3],
				  media.bmdata[i].bitmap[4],
				  media.bmdata[i].bitmap[5],
				  media.bmdata[i].bitmap[6],
				  media.bmdata[i].bitmap[7]);
		}
		tabprintf(tab, "}\n");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		printf("{\n");
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	default:
		printf("\n");
		obrace = 0;
		break;
	}
	if (str)
		free(str);
	for (i = 0; i < bcount; ++i) {
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY) {
			if (didnl == 0) {
				printf("\n");
				didnl = 1;
			}
			show_bref(fd, tab, i, &bscan[i], dofreemap);
		}
	}
	tab -= SHOW_TAB;
	if (obrace) {
		if (bref->type == HAMMER2_BREF_TYPE_INODE)
			tabprintf(tab, "} (%s.%d, \"%*.*s\")\n",
				  type_str, bi,
				  namelen, namelen, media.ipdata.filename);
		else
			tabprintf(tab, "} (%s.%d)\n", type_str,bi);
	}
}

int
cmd_hash(int ac, const char **av)
{
	int i;

	for (i = 0; i < ac; ++i) {
		printf("%016jx %s\n", dirhash(av[i], strlen(av[i])), av[i]);
	}
	return(0);
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
