/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@gmail.com>
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

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <netinet/in.h>

#include "../../../sys/net/ipfw3/ip_fw3.h"
#include "../../../sbin/ipfw3/ipfw.h"
#include "ipfw3_layer4.h"

int
char_at(char *str, char c)
{
	int pos;
	for (pos = 0; str[pos] != '\0'; pos++) {
		if (str[pos] == c)
			return pos;
	}
	return -1;
}

void
parse_tcpflag(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_LAYER4_TCPFLAG;
	(*cmd)->module = MODULE_LAYER4_ID;
	(*cmd)->len =  ((*cmd)->len&(F_NOT|F_OR))|LEN_OF_IPFWINSN;
	/* XXX TODO parse the tcpflag value and store in arg1 or arg3 */
	NEXT_ARG1;
}

void
parse_uid(ipfw_insn **cmd, int *ac, char **av[])
{
	char *end;
	uid_t uid;
	struct passwd *pwd;

	NEXT_ARG1;
	ipfw_insn_u32 *cmd32 = (ipfw_insn_u32 *)(*cmd);
	uid = strtoul(**av, &end, 0);
	pwd = (*end == '\0') ? getpwuid(uid) : getpwnam(**av);
	if (pwd == NULL)
		errx(EX_DATAERR, "uid \"%s\" not exists", **av);

	cmd32->d[0] = pwd->pw_uid;

	(*cmd)->opcode = O_LAYER4_UID;
	(*cmd)->module = MODULE_LAYER4_ID;
	(*cmd)->len = F_INSN_SIZE(ipfw_insn_u32);
	NEXT_ARG1;
}

void
parse_gid(ipfw_insn **cmd, int *ac, char **av[])
{
	char *end;
	gid_t gid;
	struct group *grp;

	NEXT_ARG1;
	ipfw_insn_u32 *cmd32 = (ipfw_insn_u32 *)(*cmd);
	gid = strtoul(**av, &end, 0);
	grp = (*end == '\0') ? getgrgid(gid) : getgrnam(**av);
	if (grp == NULL)
		errx(EX_DATAERR, "gid \"%s\" not exists", **av);

	cmd32->d[0] = grp->gr_gid;

	(*cmd)->opcode = O_LAYER4_GID;
	(*cmd)->module = MODULE_LAYER4_ID;
	(*cmd)->len = F_INSN_SIZE(ipfw_insn_u32);
	NEXT_ARG1;
}

void
parse_established(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_LAYER4_ESTABLISHED;
	(*cmd)->module = MODULE_LAYER4_ID;
	(*cmd)->len |= LEN_OF_IPFWINSN;
}

void
parse_bpf(ipfw_insn **cmd, int *ac, char **av[])
{
	struct bpf_program program;
	ipfw_insn_bpf *bpf;
	int avlen;

	NEXT_ARG1;
	(*cmd)->opcode = O_LAYER4_BPF;
	(*cmd)->module = MODULE_LAYER4_ID;

	avlen = strlen(**av);
	if (avlen > 256)
		errx(EX_DATAERR, "bpf \"%s\" too long (max 256)", **av);
	bpf = (ipfw_insn_bpf *)(*cmd);
	strcpy(bpf->bf_str, **av);
	if (pcap_compile_nopcap(65535, DLT_RAW, &program, **av, 1,
			PCAP_NETMASK_UNKNOWN))
		errx(EX_DATAERR, "bpf \"%s\" compilation error", **av);
	bpf->bf_len = program.bf_len;

	memcpy(&bpf->bf_insn, program.bf_insns,
			sizeof(struct bpf_insn) * program.bf_len);
	(*cmd)->len |= (sizeof(ipfw_insn_bpf) +
			sizeof(struct bpf_insn) * (bpf->bf_len - 1)) /
			sizeof(uint32_t);

	pcap_freecode(&program);
	NEXT_ARG1;
}

void
show_tcpflag(ipfw_insn *cmd)
{
	printf(" tcpflag %d", cmd->arg1);
}

void
show_uid(ipfw_insn *cmd)
{
	ipfw_insn_u32 *cmd32 = (ipfw_insn_u32 *)cmd;
	struct passwd *pwd = getpwuid(cmd32->d[0]);
	if (pwd){
		printf(" uid %s", pwd->pw_name);
	}else{
		printf(" uid %u", cmd32->d[0]);
	}
}

void
show_gid(ipfw_insn *cmd)
{
	ipfw_insn_u32 *cmd32 = (ipfw_insn_u32 *)cmd;
	struct group *grp = getgrgid(cmd32->d[0]);
	if (grp){
		printf(" gid %s", grp->gr_name);
	}else{
		printf(" gid %u", cmd32->d[0]);
	}
}

void
show_established(ipfw_insn *cmd)
{
	printf(" established");
}

void
show_bpf(ipfw_insn *cmd, int show_or)
{
	ipfw_insn_bpf *bpf;
	bpf = (ipfw_insn_bpf *)cmd;
	printf(" bpf \"%s\"", bpf->bf_str);
}

void
load_module(register_func function, register_keyword keyword)
{
	keyword(MODULE_LAYER4_ID, O_LAYER4_TCPFLAG, "tcpflag", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER4_ID, O_LAYER4_TCPFLAG,
			(parser_func)parse_tcpflag, (shower_func)show_tcpflag);
	keyword(MODULE_LAYER4_ID, O_LAYER4_UID, "uid", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER4_ID, O_LAYER4_UID,
			(parser_func)parse_uid, (shower_func)show_uid);
	keyword(MODULE_LAYER4_ID, O_LAYER4_GID, "gid", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER4_ID, O_LAYER4_GID,
			(parser_func)parse_gid, (shower_func)show_gid);
	keyword(MODULE_LAYER4_ID, O_LAYER4_ESTABLISHED, "established", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER4_ID, O_LAYER4_ESTABLISHED,
			(parser_func)parse_established, (shower_func)show_established);
	keyword(MODULE_LAYER4_ID, O_LAYER4_BPF, "bpf", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER4_ID, O_LAYER4_BPF,
			(parser_func)parse_bpf, (shower_func)show_bpf);
}
