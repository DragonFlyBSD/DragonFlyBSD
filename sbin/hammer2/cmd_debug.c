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

static void shell_recv(hammer2_iocom_t *iocom);
static void shell_send(hammer2_iocom_t *iocom);
static void shell_tty(hammer2_iocom_t *iocom);
static void hammer2_shell_parse(hammer2_msg_t *msg, char *cmdbuf);

/************************************************************************
 *				    SHELL				*
 ************************************************************************/

int
cmd_shell(const char *hostname)
{
	struct sockaddr_in lsin;
	struct hammer2_iocom iocom;
	hammer2_msg_t *msg;
	struct hostent *hen;
	int fd;

	/*
	 * Acquire socket and set options
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "cmd_debug: socket(): %s\n",
			strerror(errno));
		return 1;
	}

	/*
	 * Connect to the target
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = 0;
	lsin.sin_port = htons(HAMMER2_LISTEN_PORT);

	if (hostname) {
		hen = gethostbyname2(hostname, AF_INET);
		if (hen == NULL) {
			if (inet_pton(AF_INET, hostname, &lsin.sin_addr) != 1) {
				fprintf(stderr,
					"Cannot resolve %s\n", hostname);
				return 1;
			}
		} else {
			bcopy(hen->h_addr, &lsin.sin_addr, hen->h_length);
		}
	}
	if (connect(fd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(fd);
		fprintf(stderr, "debug: connect failed: %s\n",
			strerror(errno));
		return 0;
	}

	/*
	 * Run the session.  The remote end transmits our prompt.
	 */
	hammer2_iocom_init(&iocom, fd, 0);
	printf("debug: connected\n");

	msg = hammer2_allocmsg(&iocom, HAMMER2_DBG_SHELL, 0);
	hammer2_ioq_write(msg);

	hammer2_iocom_core(&iocom, shell_recv, shell_send, shell_tty);
	fprintf(stderr, "debug: disconnected\n");
	close(fd);
	return 0;
}

/*
 * Callback from hammer2_iocom_core() when messages might be present
 * on the socket.
 */
static
void
shell_recv(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0 &&
	       (msg = hammer2_ioq_read(iocom)) != NULL) {

		switch(msg->any.head.cmd & HAMMER2_MSGF_CMDSWMASK) {
		case HAMMER2_LNK_ERROR:
			fprintf(stderr, "Link Error: %d\n",
				msg->any.head.error);
			break;
		case HAMMER2_DBG_SHELL:
			/*
			 * We send the commands, not accept them.
			 */
			hammer2_replymsg(msg, HAMMER2_MSG_ERR_UNKNOWN);
			hammer2_freemsg(msg);
			break;
		case HAMMER2_DBG_SHELL | HAMMER2_MSGF_REPLY:
			/*
			 * A reply from the remote is data we copy to stdout.
			 */
			if (msg->aux_size) {
				msg->aux_data[msg->aux_size - 1] = 0;
				write(1, msg->aux_data, strlen(msg->aux_data));
			} else {
				write(1, "debug> ", 7);
			}
			hammer2_freemsg(msg);
			break;
		default:
			assert((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0);
			fprintf(stderr, "Unknown message: %08x\n",
				msg->any.head.cmd);
			hammer2_replymsg(msg, HAMMER2_MSG_ERR_UNKNOWN);
			break;
		}
	}
	if (iocom->ioq_rx.error) {
		fprintf(stderr, "node_master_recv: comm error %d\n",
			iocom->ioq_rx.error);
	}
}

/*
 * Callback from hammer2_iocom_core() when messages might be transmittable
 * to the socket.
 */
static
void
shell_send(hammer2_iocom_t *iocom)
{
	hammer2_iocom_flush(iocom);
}

static
void
shell_tty(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;
	char buf[256];
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) != NULL) {
		len = strlen(buf);
		if (len && buf[len - 1] == '\n')
			buf[--len] = 0;
		++len;
		msg = hammer2_allocmsg(iocom, HAMMER2_DBG_SHELL, len);
		bcopy(buf, msg->aux_data, len);
		hammer2_ioq_write(msg);
	} else {
		/*
		 * Set EOF flag without setting any error code for normal
		 * EOF.
		 */
		iocom->flags |= HAMMER2_IOCOMF_EOF;
	}
}

/*
 * This is called from the master node to process a received debug
 * shell command.  We process the command, outputting the results,
 * then finish up by outputting another prompt.
 */
void
hammer2_shell_remote(hammer2_msg_t *msg)
{
	/* hammer2_iocom_t *iocom = msg->iocom; */

	if (msg->aux_data)
		msg->aux_data[msg->aux_size - 1] = 0;
	if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
		/*
		 * A reply just prints out the string.  No newline is added
		 * (it is expected to be embedded if desired).
		 */
		if (msg->aux_data)
			write(2, msg->aux_data, strlen(msg->aux_data));
		hammer2_freemsg(msg);
	} else {
		/*
		 * Otherwise this is a command which we must process.
		 * When we are finished we generate a final reply.
		 */
		hammer2_shell_parse(msg, msg->aux_data);
		hammer2_replymsg(msg, 0);
	}
}

static void
hammer2_shell_parse(hammer2_msg_t *msg, char *cmdbuf)
{
	/* hammer2_iocom_t *iocom = msg->iocom; */
	char *cmd = strsep(&cmdbuf, " \t");

	if (cmd == NULL || *cmd == 0) {
		;
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		msg_printf(msg, "help        Command help\n");
	} else {
		msg_printf(msg, "Unrecognized command: %s\n", cmd);
	}
}

/*
 * Returns text debug output to the original defined by (msg).  (msg) is
 * not modified and stays intact.
 */
void
msg_printf(hammer2_msg_t *msg, const char *ctl, ...)
{
	/* hammer2_iocom_t *iocom = msg->iocom; */
	hammer2_msg_t *rmsg;
	va_list va;
	char buf[1024];
	size_t len;

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	va_end(va);
	len = strlen(buf) + 1;

	rmsg = hammer2_allocreply(msg, HAMMER2_DBG_SHELL, len);
	bcopy(buf, rmsg->aux_data, len);

	hammer2_ioq_write(rmsg);
}

/************************************************************************
 *				    SHOW				*
 ************************************************************************/

static void show_bref(int fd, int tab, int bi, hammer2_blockref_t *bref);
static void tabprintf(int tab, const char *ctl, ...);

int
cmd_show(const char *devpath)
{
	hammer2_blockref_t broot;
	int fd;

	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	bzero(&broot, sizeof(broot));
	broot.type = HAMMER2_BREF_TYPE_VOLUME;
	broot.data_off = 0 | HAMMER2_PBUFRADIX;
	show_bref(fd, 0, 0, &broot);
	close(fd);

	return 0;
}

static void
show_bref(int fd, int tab, int bi, hammer2_blockref_t *bref)
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int bcount;
	int i;
	int didnl;
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
	default:
		type_str = "unknown";
		break;
	}


	tabprintf(tab, "%s.%-3d %016jx/%-2d mir=%016jx mod=%016jx ",
	       type_str, bi,
	       bref->key, bref->keybits,
	       bref->mirror_tid, bref->modify_tid);
	tab += SHOW_TAB;

	bytes = (size_t)1 << (bref->data_off & HAMMER2_OFF_MASK_RADIX);
	if (bytes > sizeof(media)) {
		printf("(bad block size %zd)\n", bytes);
		return;
	}
	if (bref->type != HAMMER2_BREF_TYPE_DATA || VerboseOpt >= 1) {
		lseek(fd, bref->data_off & ~HAMMER2_OFF_MASK_RADIX, 0);
		if (read(fd, &media, bytes) != (ssize_t)bytes) {
			printf("(media read failed)\n");
			return;
		}
	}

	bscan = NULL;
	bcount = 0;
	didnl = 0;

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
		tabprintf(tab, "filename \"%s\"\n", media.ipdata.filename);
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
			tabprintf(tab, "pfs_id   %s\n",
				  hammer2_uuid_to_str(&media.ipdata.pfs_id,
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
		bscan = &media.npdata.blockref[0];
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
		bscan = &media.voldata.sroot_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		printf("{\n");
		break;
	default:
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
			show_bref(fd, tab, i, &bscan[i]);
		}
	}
	tab -= SHOW_TAB;
	if (obrace) {
		if (bref->type == HAMMER2_BREF_TYPE_INODE)
			tabprintf(tab, "} (%s.%d, \"%s\")\n",
				  type_str, bi, media.ipdata.filename);
		else
			tabprintf(tab, "} (%s.%d)\n", type_str,bi);
	}
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
