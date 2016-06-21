/*
 * Copyright (c) 2014 - 2016 The DragonFly Project.  All rights reserved.
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


#ifndef _IPFW3_H_
#define _IPFW3_H_

/*
 * This macro returns the size of a struct sockaddr when passed
 * through a routing socket. Basically we round up sa_len to
 * a multiple of sizeof(long), with a minimum of sizeof(long).
 * The check for a NULL pointer is just a convenience, probably never used.
 * The case sa_len == 0 should only apply to empty structures.
 */
#define SA_SIZE(sa)						\
	( (!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?	\
	sizeof(long)		:				\
	1 + ( (((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

/*
 * Definition of a port range, and macros to deal with values.
 * FORMAT: HI 16-bits == first port in range, 0 == all ports.
 *		 LO 16-bits == number of ports in range
 * NOTES: - Port values are not stored in network byte order.
 */


#define GETLOPORT(x)	((x) >> 0x10)
#define GETNUMPORTS(x)	((x) & 0x0000ffff)
#define GETHIPORT(x)	(GETLOPORT((x)) + GETNUMPORTS((x)))

/* Set y to be the low-port value in port_range variable x. */
#define SETLOPORT(x, y) ((x) = ((x) & 0x0000ffff) | ((y) << 0x10))

/* Set y to be the number of ports in port_range variable x. */
#define SETNUMPORTS(x, y) ((x) = ((x) & 0xffff0000) | (y))

#define INC_ARGCV() do {			\
	(*_av)++;				\
	(*_ac)--;				\
	av = *_av;				\
	ac = *_ac;				\
} while (0)


enum tokens {
	TOK_NULL=0,

	TOK_IP,
	TOK_IF,
	TOK_ALOG,
	TOK_DENY_INC,
	TOK_SAME_PORTS,
	TOK_UNREG_ONLY,
	TOK_RESET_ADDR,
	TOK_ALIAS_REV,
	TOK_PROXY_ONLY,
	TOK_REDIR_ADDR,
	TOK_REDIR_PORT,
	TOK_REDIR_PROTO,

	TOK_PIPE,
	TOK_QUEUE,
	TOK_PLR,
	TOK_NOERROR,
	TOK_BUCKETS,
	TOK_DSTIP,
	TOK_SRCIP,
	TOK_DSTPORT,
	TOK_SRCPORT,
	TOK_ALL,
	TOK_MASK,
	TOK_BW,
	TOK_DELAY,
	TOK_RED,
	TOK_GRED,
	TOK_DROPTAIL,
	TOK_PROTO,
	TOK_WEIGHT,
};

struct char_int_map {
	char *key;
	int val;
};

typedef void (*parser_func)(ipfw_insn **cmd,int *ac, char **av[]);
typedef void (*shower_func)(ipfw_insn *cmd, int show_or);
typedef void (*register_func)(int module, int opcode,
		parser_func parser, shower_func shower);
typedef void (*register_keyword)(int module, int opcode, char *word, int type);
void register_ipfw_keyword(int module, int opcode, char *word, int type);
void register_ipfw_func(int module, int opcode,
		parser_func parser, shower_func shower);
typedef void (*init_module)(register_func func, register_keyword keyword);
int do_get_x(int optname, void *rule, int *optlen);
int do_set_x(int optname, void *rule, int optlen);

int match_token(struct char_int_map *table, char *string);
#endif
