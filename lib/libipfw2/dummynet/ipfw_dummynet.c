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

#include <stdio.h>
#include <stdlib.h>

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <netinet/in.h>

#include "../../../sys/net/ipfw2/ip_fw2.h"
#include "../../../sbin/ipfw2/ipfw.h"
#include "ipfw_dummynet.h"

void
parse_pipe(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_DUMMYNET_PIPE;
	(*cmd)->module = MODULE_DUMMYNET_ID;
	(*cmd)->len = ((*cmd)->len&(F_NOT|F_OR))|LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
parse_queue(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_DUMMYNET_QUEUE;
	(*cmd)->module = MODULE_DUMMYNET_ID;
	(*cmd)->len = ((*cmd)->len&(F_NOT|F_OR))|LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
show_pipe(ipfw_insn *cmd)
{
	printf(" pipe %u", cmd->arg1);
}

void
show_queue(ipfw_insn *cmd)
{
	printf(" queue %u", cmd->arg1);
}

void
load_module(register_func function, register_keyword keyword)
{
	keyword(MODULE_DUMMYNET_ID,O_DUMMYNET_PIPE,"pipe", IPFW_KEYWORD_TYPE_ACTION);
	function(MODULE_DUMMYNET_ID,O_DUMMYNET_PIPE,
			(parser_func)parse_pipe, (shower_func)show_pipe);

	keyword(MODULE_DUMMYNET_ID,O_DUMMYNET_QUEUE,"queue", IPFW_KEYWORD_TYPE_ACTION);
	function(MODULE_DUMMYNET_ID,O_DUMMYNET_QUEUE,
			(parser_func)parse_queue, (shower_func)show_queue);
}

