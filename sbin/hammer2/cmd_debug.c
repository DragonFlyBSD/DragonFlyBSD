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

static void shell_rcvmsg(hammer2_msg_t *msg);
static void shell_ttymsg(hammer2_iocom_t *iocom);
static void hammer2_shell_parse(hammer2_msg_t *msg);

/************************************************************************
 *				    SHELL				*
 ************************************************************************/

int
cmd_shell(const char *hostname)
{
	struct hammer2_iocom iocom;
	hammer2_msg_t *msg;
	int fd;

	/*
	 * Connect to the target
	 */
	fd = hammer2_connect(hostname);
	if (fd < 0)
		return 1;

	/*
	 * Run the session.  The remote end transmits our prompt.
	 */
	hammer2_iocom_init(&iocom, fd, 0, NULL, shell_rcvmsg, shell_ttymsg);
	fcntl(0, F_SETFL, O_NONBLOCK);
	printf("debug: connected\n");

	msg = hammer2_msg_alloc(iocom.router, 0, HAMMER2_DBG_SHELL,
				NULL, NULL);
	hammer2_msg_write(msg);
	hammer2_iocom_core(&iocom);
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
shell_rcvmsg(hammer2_msg_t *msg)
{
	switch(msg->any.head.cmd & HAMMER2_MSGF_TRANSMASK) {
	case HAMMER2_LNK_ERROR:
	case HAMMER2_LNK_ERROR | HAMMER2_MSGF_REPLY:
		/*
		 * One-way non-transactional LNK_ERROR messages typically
		 * indicate a connection failure.  Error code 0 is used by
		 * the debug shell to indicate no more results from last cmd.
		 */
		if (msg->any.head.error) {
			fprintf(stderr, "Stream failure: %s\n",
				hammer2_msg_str(msg));
		} else {
			write(1, "debug> ", 7);
		}
		break;
	case HAMMER2_LNK_ERROR | HAMMER2_MSGF_DELETE:
		/* ignore termination of LNK_CONN */
		break;
	case HAMMER2_DBG_SHELL:
		/*
		 * We send the commands, not accept them.
		 * (one-way message, not transactional)
		 */
		hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		break;
	case HAMMER2_DBG_SHELL | HAMMER2_MSGF_REPLY:
		/*
		 * A reply from the remote is data we copy to stdout.
		 * (one-way message, not transactional)
		 */
		if (msg->aux_size) {
			msg->aux_data[msg->aux_size - 1] = 0;
			write(1, msg->aux_data, strlen(msg->aux_data));
		}
		break;
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_CREATE:
		fprintf(stderr, "Debug Shell is ignoring received LNK_CONN\n");
		hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		break;
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_DELETE:
		break;
	default:
		/*
		 * Ignore any unknown messages, Terminate any unknown
		 * transactions with an error.
		 */
		fprintf(stderr, "Unknown message: %s\n", hammer2_msg_str(msg));
		if (msg->any.head.cmd & HAMMER2_MSGF_CREATE)
			hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		if (msg->any.head.cmd & HAMMER2_MSGF_DELETE)
			hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		break;
	}
}

static
void
shell_ttymsg(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;
	char buf[256];
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) != NULL) {
		len = strlen(buf);
		if (len && buf[len - 1] == '\n')
			buf[--len] = 0;
		++len;
		msg = hammer2_msg_alloc(iocom->router, len, HAMMER2_DBG_SHELL,
					NULL, NULL);
		bcopy(buf, msg->aux_data, len);
		hammer2_msg_write(msg);
	} else if (feof(stdin)) {
		/*
		 * Set EOF flag without setting any error code for normal
		 * EOF.
		 */
		iocom->flags |= HAMMER2_IOCOMF_EOF;
	} else {
		clearerr(stdin);
	}
}

/*
 * This is called from the master node to process a received debug
 * shell command.  We process the command, outputting the results,
 * then finish up by outputting another prompt.
 */
void
hammer2_msg_dbg(hammer2_msg_t *msg)
{
	switch(msg->any.head.cmd & HAMMER2_MSGF_CMDSWMASK) {
	case HAMMER2_DBG_SHELL:
		/*
		 * This is a command which we must process.
		 * When we are finished we generate a final reply.
		 */
		if (msg->aux_data)
			msg->aux_data[msg->aux_size - 1] = 0;
		hammer2_shell_parse(msg);
		hammer2_msg_reply(msg, 0);
		break;
	case HAMMER2_DBG_SHELL | HAMMER2_MSGF_REPLY:
		/*
		 * A reply just prints out the string.  No newline is added
		 * (it is expected to be embedded if desired).
		 */
		if (msg->aux_data)
			msg->aux_data[msg->aux_size - 1] = 0;
		if (msg->aux_data)
			write(2, msg->aux_data, strlen(msg->aux_data));
		break;
	default:
		hammer2_msg_reply(msg, HAMMER2_MSG_ERR_NOSUPP);
		break;
	}
}

static void shell_span(hammer2_router_t *router, char *cmdbuf);
/*static void shell_tree(hammer2_router_t *router, char *cmdbuf);*/

static void
hammer2_shell_parse(hammer2_msg_t *msg)
{
	hammer2_router_t *router = msg->router;
	char *cmdbuf = msg->aux_data;
	char *cmd = strsep(&cmdbuf, " \t");

	if (cmd == NULL || *cmd == 0) {
		;
	} else if (strcmp(cmd, "span") == 0) {
		shell_span(router, cmdbuf);
	} else if (strcmp(cmd, "tree") == 0) {
		shell_tree(router, cmdbuf);
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		router_printf(router, "help            Command help\n");
		router_printf(router, "span <host>     Span to target host\n");
		router_printf(router, "tree            Dump spanning tree\n");
	} else {
		router_printf(router, "Unrecognized command: %s\n", cmd);
	}
}

static void
shell_span(hammer2_router_t *router, char *cmdbuf)
{
	const char *hostname = strsep(&cmdbuf, " \t");
	pthread_t thread;
	int fd;

	/*
	 * Connect to the target
	 */
	if (hostname == NULL) {
		fd = -1;
	} else {
		fd = hammer2_connect(hostname);
	}

	/*
	 * Start master service
	 */
	if (fd < 0) {
		router_printf(router, "Connection to %s failed\n", hostname);
	} else {
		router_printf(router, "Connected to %s\n", hostname);
		pthread_create(&thread, NULL,
			       master_service, (void *)(intptr_t)fd);
		/*pthread_join(thread, &res);*/
	}
}

/*
 * Returns text debug output to the original defined by (msg).  (msg) is
 * not modified and stays intact.  We use a one-way message with REPLY set
 * to distinguish between a debug command and debug terminal output.
 *
 * To prevent loops router_printf() can filter the message (cmd) related
 * to the router_printf().  We filter out DBG messages.
 */
void
router_printf(hammer2_router_t *router, const char *ctl, ...)
{
	hammer2_msg_t *rmsg;
	va_list va;
	char buf[1024];
	size_t len;

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	va_end(va);
	len = strlen(buf) + 1;

	rmsg = hammer2_msg_alloc(router, len, HAMMER2_DBG_SHELL |
					      HAMMER2_MSGF_REPLY,
				 NULL, NULL);
	bcopy(buf, rmsg->aux_data, len);

	hammer2_msg_write(rmsg);
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
	fd = hammer2_connect(hostname);
	if (fd < 0)
		return 1;

	printf("debugspan: connected to %s, starting CONN/SPAN\n", hostname);
	pthread_create(&thread, NULL, master_service, (void *)(intptr_t)fd);
	pthread_join(thread, &res);
	return(0);
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
	if (bytes < HAMMER2_MINIOSIZE || bytes > sizeof(media)) {
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
