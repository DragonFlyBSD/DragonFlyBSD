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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/bin/ps/extern.h,v 1.6 2004/06/21 00:47:57 hmp Exp $
 */

struct kinfo;
struct nlist;
struct var;
struct varent;

extern fixpt_t ccpu;
extern int eval, fscale, mempages, nlistread, rawcpu, cflag;
extern int sumrusage, termwidth, totwidth;
extern VAR var[];
extern VARENT *vhead;
extern struct timeval btime;

__BEGIN_DECLS
void	 command (const KINFO *, const VARENT *);
void	 cputime (const KINFO *, const VARENT *);
int	 donlist (void);
void	 evar (const KINFO *, const VARENT *);
const char *fmt_argv (char **, char *, int);
double	 getpcpu (const KINFO *);
double	 getpmem (const KINFO *);
void	 logname (const KINFO *, const VARENT *);
void	 longtname (const KINFO *, const VARENT *);
void	 lstarted (const KINFO *, const VARENT *);
void	 maxrss (const KINFO *, const VARENT *);
void	 nlisterr (struct nlist *);
void	 p_rssize (const KINFO *, const VARENT *);
void	 pagein (const KINFO *, const VARENT *);
void	 parsefmt (const char *);
void	 pcpu (const KINFO *, const VARENT *);
void	 pnice (const KINFO *, const VARENT *);
void	 pmem (const KINFO *, const VARENT *);
void	 pri (const KINFO *, const VARENT *);
void	 rtprior (const KINFO *, const VARENT *);
void	 printheader (void);
void	 pvar (const KINFO *, const VARENT *);
void	 tvar (const KINFO *, const VARENT *);
void	 rssize (const KINFO *, const VARENT *);
void	 runame (const KINFO *, const VARENT *);
int	 s_runame (const KINFO *);
void	 rvar (const KINFO *, const VARENT *);
void	 showkey (void);
void	 started (const KINFO *, const VARENT *);
void	 state (const KINFO *, const VARENT *);
void	 tdev (const KINFO *, const VARENT *);
void	 tname (const KINFO *, const VARENT *);
void	 tsize (const KINFO *, const VARENT *);
void	 ucomm (const KINFO *, const VARENT *);
void	 uname (const KINFO *, const VARENT *);
int	 s_uname (const KINFO *);
void	 uvar (const KINFO *, const VARENT *);
void	 vsize (const KINFO *, const VARENT *);
void	 wchan (const KINFO *, const VARENT *);
__END_DECLS
