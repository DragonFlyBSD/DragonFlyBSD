/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
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

#ifndef _IPFW3NAT_H_
#define _IPFW3NAT_H_

#define NAT_BUF_LEN	1024
#define port_range u_long

void nat_config(int ac, char **av);
void nat_show_config(char *buf);
void nat_show(int ac, char **av);
int setup_redir_port(char *spool_buf, int len, int *_ac, char ***_av);
int setup_redir_proto(char *spool_buf, int len, int *_ac, char ***_av);
int str2proto(const char* str);
int str2addr_portrange (const char* str, struct in_addr* addr,
		char* proto, port_range *portRange);
void set_addr_dynamic(const char *ifn, struct cfg_nat *n);
int setup_redir_addr(char *spool_buf, int len, int *_ac, char ***_av);
int str2portrange(const char* str, const char* proto, port_range *portRange);
void str2addr(const char* str, struct in_addr* addr);
void nat_delete_config(int ac, char *av[]);
void nat_show_state(int ac, char **av);
int get_kern_boottime(void);

void nat_flush(void);
void nat_main(int ac, char **av);
#endif
