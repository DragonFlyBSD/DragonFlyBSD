/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)extern.h	8.3 (Berkeley) 4/2/94
 * $FreeBSD: src/bin/ps/extern.h,v 1.9 1999/08/27 23:14:50 peter Exp $
 * $DragonFly: src/bin/ps/extern.h,v 1.14 2007/08/14 20:29:06 dillon Exp $
 */

struct kinfo;
struct nlist;
struct var;
struct varent;

extern int eval, fscale, mempages, nlistread, rawcpu, cflag, showtid;
extern int sumrusage, termwidth, totwidth;
extern int numcpus;
extern STAILQ_HEAD(varent_head, varent) var_head;
extern struct timeval btime;

__BEGIN_DECLS
void	 command(const KINFO *, const struct varent *);
void	 cputime(const KINFO *, const struct varent *);
int	 donlist(void);
void	 evar(const KINFO *, const struct varent *);
const char *fmt_argv(char **, char *, int);
double	 getpcpu(const KINFO *);
double	 getpmem(const KINFO *);
void	 logname(const KINFO *, const struct varent *);
void	 longtname(const KINFO *, const struct varent *);
void	 lstarted(const KINFO *, const struct varent *);
void	 maxrss(const KINFO *, const struct varent *);
void	 nlisterr(struct nlist *);
void	 p_rssize(const KINFO *, const struct varent *);
void	 pagein(const KINFO *, const struct varent *);
void	 parsefmt(const char *);
void	 insert_tid_in_fmt(void);
void	 pcpu(const KINFO *, const struct varent *);
void	 pnice(const KINFO *, const struct varent *);
void	 pmem(const KINFO *, const struct varent *);
void	 pri(const KINFO *, const struct varent *);
void	 tdpri(const KINFO *, const struct varent *);
void	 rtprior(const KINFO *, const struct varent *);
void	 printheader(void);
void	 pvar(const KINFO *, const struct varent *);
void	 lpvar(const KINFO *, const struct varent *);
void	 lpest(const KINFO *, const struct varent *);
void	 tvar(const KINFO *, const struct varent *);
void	 rssize(const KINFO *, const struct varent *);
void	 runame(const KINFO *, const struct varent *);
int	 s_runame(const KINFO *);
void	 rvar(const KINFO *, const struct varent *);
void	 showkey(void);
void	 started(const KINFO *, const struct varent *);
void	 state(const KINFO *, const struct varent *);
void	 tdev(const KINFO *, const struct varent *);
void	 tname(const KINFO *, const struct varent *);
void	 tsize(const KINFO *, const struct varent *);
void	 ucomm(const KINFO *, const struct varent *);
void	 uname(const KINFO *, const struct varent *);
int	 s_uname(const KINFO *);
void	 vsize(const KINFO *, const struct varent *);
void	 wchan(const KINFO *, const struct varent *);
__END_DECLS
