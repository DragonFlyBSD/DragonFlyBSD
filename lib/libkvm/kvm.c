/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley.
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
 * @(#)kvm.c	8.2 (Berkeley) 2/13/94
 * $FreeBSD: src/lib/libkvm/kvm.c,v 1.12.2.3 2002/09/13 14:53:43 nectar Exp $
 */

#include <sys/user.h>	/* MUST BE FIRST */
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/linker.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/swap_pager.h>

#include <machine/vmparam.h>

#include <ctype.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "kvm_private.h"

/* from src/lib/libc/gen/nlist.c */
int __fdnlist		(int, struct nlist *);

char *
kvm_geterr(kvm_t *kd)
{
	return (kd->errbuf);
}

/*
 * Report an error using printf style arguments.  "program" is kd->program
 * on hard errors, and 0 on soft errors, so that under sun error emulation,
 * only hard errors are printed out (otherwise, programs like gdb will
 * generate tons of error messages when trying to access bogus pointers).
 */
void
_kvm_err(kvm_t *kd, const char *program, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (program != NULL) {
		(void)fprintf(stderr, "%s: ", program);
		(void)vfprintf(stderr, fmt, ap);
		(void)fputc('\n', stderr);
	} else
		(void)vsnprintf(kd->errbuf,
		    sizeof(kd->errbuf), (char *)fmt, ap);

	va_end(ap);
}

void
_kvm_syserr(kvm_t *kd, const char *program, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	if (program != NULL) {
		(void)fprintf(stderr, "%s: ", program);
		(void)vfprintf(stderr, fmt, ap);
		(void)fprintf(stderr, ": %s\n", strerror(errno));
	} else {
		char *cp = kd->errbuf;

		(void)vsnprintf(cp, sizeof(kd->errbuf), (char *)fmt, ap);
		n = strlen(cp);
		(void)snprintf(&cp[n], sizeof(kd->errbuf) - n, ": %s",
		    strerror(errno));
	}
	va_end(ap);
}

void *
_kvm_malloc(kvm_t *kd, size_t n)
{
	void *p;

	if ((p = calloc(n, sizeof(char))) == NULL)
		_kvm_err(kd, kd->program, "can't allocate %zd bytes: %s",
			 n, strerror(errno));
	return (p);
}

static int
is_proc_mem(const char *p)
{
	static char proc[] = "/proc/";
	static char mem[] = "/mem";
	if (strncmp(proc, p, sizeof(proc) - 1))
		return 0;
	p += sizeof(proc) - 1;
	for (; *p != '\0'; ++p)
		if (!isdigit(*p))
			break;
	if (!isdigit(*(p - 1)))
		return 0;
	return !strncmp(p, mem, sizeof(mem) - 1);
}

static kvm_t *
_kvm_open(kvm_t *kd, const char *uf, const char *mf, int flag, char *errout)
{
	struct stat st;

	kd->vmfd = -1;
	kd->pmfd = -1;
	kd->nlfd = -1;
	kd->vmst = 0;
	kd->procbase = NULL;
	kd->procend = NULL;
	kd->argspc = 0;
	kd->argv = 0;
	kd->flags = 0;

	if (uf == NULL)
		uf = getbootfile();
	else if (strlen(uf) >= MAXPATHLEN) {
		_kvm_err(kd, kd->program, "exec file name too long");
		goto failed;
	}
	if (flag & ~O_RDWR) {
		_kvm_err(kd, kd->program, "bad flags arg");
		goto failed;
	}
	if (mf == NULL)
		mf = _PATH_MEM;

	if ((kd->pmfd = open(mf, flag, 0)) < 0) {
		_kvm_syserr(kd, kd->program, "%s", mf);
		goto failed;
	}
	if (fstat(kd->pmfd, &st) < 0) {
		_kvm_syserr(kd, kd->program, "%s", mf);
		goto failed;
	}
	if (fcntl(kd->pmfd, F_SETFD, FD_CLOEXEC) < 0) {
		_kvm_syserr(kd, kd->program, "%s", mf);
		goto failed;
	}
	if (S_ISCHR(st.st_mode)) {
		/*
		 * If this is a character special device, then check that
		 * it's /dev/mem.  If so, open kmem too.  (Maybe we should
		 * make it work for either /dev/mem or /dev/kmem -- in either
		 * case you're working with a live kernel.)
		 */
		if (strcmp(mf, _PATH_DEVNULL) == 0) {
			kd->vmfd = open(_PATH_DEVNULL, O_RDONLY);
		} else {
			if ((kd->vmfd = open(_PATH_KMEM, flag)) < 0) {
				_kvm_syserr(kd, kd->program, "%s", _PATH_KMEM);
				goto failed;
			}
			if (fcntl(kd->vmfd, F_SETFD, FD_CLOEXEC) < 0) {
				_kvm_syserr(kd, kd->program, "%s", _PATH_KMEM);
				goto failed;
			}
		}
		kd->flags |= KVMF_HOST;
	} else {
		/*
		 * Crash dump or vkernel /proc/$pid/mem file:
		 * can't use kldsym, we are going to need ->nlfd
		 */
		if ((kd->nlfd = open(uf, O_RDONLY, 0)) < 0) {
			_kvm_syserr(kd, kd->program, "%s", uf);
			goto failed;
		}
		if (fcntl(kd->nlfd, F_SETFD, FD_CLOEXEC) < 0) {
			_kvm_syserr(kd, kd->program, "%s", uf);
			goto failed;
		}
		if(is_proc_mem(mf)) {
			/*
			 * It's /proc/$pid/mem, so we rely on the host
			 * kernel to do address translation for us.
			 */
			kd->vmfd = kd->pmfd;
			kd->pmfd = -1;
			kd->flags |= KVMF_VKERN;
		} else {
			if (st.st_size <= 0) {
				errno = EINVAL;
				_kvm_syserr(kd, kd->program, "empty file");
				goto failed;
			}
			
			/*
			 * This is a crash dump.
			 * Initialize the virtual address translation machinery,
			 * but first setup the namelist fd.
			 */
			if (_kvm_initvtop(kd) < 0)
				goto failed;
		}
	}
	return (kd);
failed:
	/*
	 * Copy out the error if doing sane error semantics.
	 */
	if (errout != NULL)
		strlcpy(errout, kd->errbuf, _POSIX2_LINE_MAX);
	(void)kvm_close(kd);
	return (0);
}

kvm_t *
kvm_openfiles(const char *uf, const char *mf, const char *sf, int flag,
	      char *errout)
{
	kvm_t *kd;

	if ((kd = malloc(sizeof(*kd))) == NULL) {
		(void)strlcpy(errout, strerror(errno), _POSIX2_LINE_MAX);
		return (0);
	}
	memset(kd, 0, sizeof(*kd));
	kd->program = 0;
	return (_kvm_open(kd, uf, mf, flag, errout));
}

kvm_t *
kvm_open(const char *uf, const char *mf, const char *sf, int flag,
	 const char *errstr)
{
	kvm_t *kd;

	if ((kd = malloc(sizeof(*kd))) == NULL) {
		if (errstr != NULL)
			(void)fprintf(stderr, "%s: %s\n",
				      errstr, strerror(errno));
		return (0);
	}
	memset(kd, 0, sizeof(*kd));
	kd->program = errstr;
	return (_kvm_open(kd, uf, mf, flag, NULL));
}

int
kvm_close(kvm_t *kd)
{
	int error = 0;

	if (kd->pmfd >= 0)
		error |= close(kd->pmfd);
	if (kd->vmfd >= 0)
		error |= close(kd->vmfd);
	if (kd->nlfd >= 0)
		error |= close(kd->nlfd);
	if (kd->vmst)
		_kvm_freevtop(kd);
	if (kd->procbase != NULL)
		free(kd->procbase);
	if (kd->argv != 0)
		free((void *)kd->argv);
	free((void *)kd);

	return (0);
}

int
kvm_nlist(kvm_t *kd, struct nlist *nl)
{
	struct nlist *p;
	int nvalid;
	struct kld_sym_lookup lookup;
	int error;

	/*
	 * If we can't use the kld symbol lookup, revert to the
	 * slow library call.
	 */
	if (!kvm_ishost(kd))
		return (__fdnlist(kd->nlfd, nl));

	/*
	 * We can use the kld lookup syscall.  Go through each nlist entry
	 * and look it up with a kldsym(2) syscall.
	 */
	nvalid = 0;
	for (p = nl; p->n_name && p->n_name[0]; ++p) {
		lookup.version = sizeof(lookup);
		lookup.symname = p->n_name;
		lookup.symvalue = 0;
		lookup.symsize = 0;

		if (lookup.symname[0] == '_')
			lookup.symname++;

		if (kldsym(0, KLDSYM_LOOKUP, &lookup) != -1) {
			p->n_type = N_TEXT;
			p->n_other = 0;
			p->n_desc = 0;
			p->n_value = lookup.symvalue;
			++nvalid;
			/* lookup.symsize */
		}
	}
	/*
	 * Return the number of entries that weren't found. If they exist,
	 * also fill internal error buffer.
	 */
	error = ((p - nl) - nvalid);
	if (error)
		_kvm_syserr(kd, kd->program, "kvm_nlist");
	return (error);
}

ssize_t
kvm_read(kvm_t *kd, u_long kva, void *buf, size_t len)
{
	int cc;
	void *cp;

	if (kvm_notrans(kd)) {
		/*
		 * We're using /dev/kmem.  Just read straight from the
		 * device and let the active kernel do the address translation.
		 */
		errno = 0;
		if (lseek(kd->vmfd, (off_t)kva, 0) == -1 && errno != 0) {
			_kvm_err(kd, 0, "invalid address (%lx)", kva);
			return (-1);
		}

		/*
		 * Try to pre-fault the user memory to reduce instances of
		 * races within the kernel.  XXX workaround for kernel bug
		 * where kernel does a sanity check, but user faults during
		 * the copy can block and race against another kernel entity
		 * unmapping the memory in question.
		 */
		bzero(buf, len);
		cc = read(kd->vmfd, buf, len);
		if (cc < 0) {
			_kvm_syserr(kd, 0, "kvm_read");
			return (-1);
		} else if (cc < len)
			_kvm_err(kd, kd->program, "short read");
		return (cc);
	} else {
		cp = buf;
		while (len > 0) {
			off_t pa;

			cc = _kvm_kvatop(kd, kva, &pa);
			if (cc == 0)
				return (-1);
			if (cc > len)
				cc = len;
			errno = 0;
			if (lseek(kd->pmfd, pa, 0) == -1 && errno != 0) {
				_kvm_syserr(kd, 0, _PATH_MEM);
				break;
			}
			bzero(cp, cc);
			cc = read(kd->pmfd, cp, cc);
			if (cc < 0) {
				_kvm_syserr(kd, kd->program, "kvm_read");
				break;
			}
			/*
			 * If kvm_kvatop returns a bogus value or our core
			 * file is truncated, we might wind up seeking beyond
			 * the end of the core file in which case the read will
			 * return 0 (EOF).
			 */
			if (cc == 0)
				break;
			cp = (char *)cp + cc;
			kva += cc;
			len -= cc;
		}
		return ((char *)cp - (char *)buf);
	}
	/* NOTREACHED */
}

char *
kvm_readstr(kvm_t *kd, u_long kva, char *buf, size_t *lenp)
{
	size_t len, cc, pos;
	char ch;
	int asize = -1;

	if (buf == NULL) {
		asize = len = 16;
		buf = malloc(len);
		if (buf == NULL) {
			_kvm_syserr(kd, kd->program, "kvm_readstr");
			return NULL;
		}
	} else {
		len = *lenp;
	}

	if (kvm_notrans(kd)) {
		/*
		 * We're using /dev/kmem.  Just read straight from the
		 * device and let the active kernel do the address translation.
		 */
		errno = 0;
		if (lseek(kd->vmfd, (off_t)kva, 0) == -1 && errno != 0) {
			_kvm_err(kd, 0, "invalid address (%lx)", kva);
			return NULL;
		}

		for (pos = 0, ch = -1; ch != 0; pos++) {
			cc = read(kd->vmfd, &ch, 1);
			if ((ssize_t)cc < 0) {
				_kvm_syserr(kd, 0, "kvm_readstr");
				return NULL;
			} else if (cc < 1)
				_kvm_err(kd, kd->program, "short read");
			if (pos == asize) {
				buf = realloc(buf, asize *= 2);
				if (buf == NULL) {
					_kvm_syserr(kd, kd->program, "kvm_readstr");
					return NULL;
				}
				len = asize;
			}
			if (pos < len)
				buf[pos] = ch;
		}

		if (lenp != NULL)
			*lenp = pos;
		if (pos > len)
			return NULL;
		else
			return buf;
	} else {
		size_t left = 0;
		for (pos = 0, ch = -1; ch != 0; pos++, left--, kva++) {
			if (left == 0) {
				off_t pa;

				left = _kvm_kvatop(kd, kva, &pa);
				if (left == 0)
					return NULL;
				errno = 0;
				if (lseek(kd->pmfd, (off_t)pa, 0) == -1 && errno != 0) {
					_kvm_syserr(kd, 0, _PATH_MEM);
					return NULL;
				}
			}
			cc = read(kd->pmfd, &ch, 1);
			if ((ssize_t)cc < 0) {
				_kvm_syserr(kd, 0, "kvm_readstr");
				return NULL;
			} else if (cc < 1)
				_kvm_err(kd, kd->program, "short read");
			if (pos == asize) {
				buf = realloc(buf, asize *= 2);
				if (buf == NULL) {
					_kvm_syserr(kd, kd->program, "kvm_readstr");
					return NULL;
				}
				len = asize;
			}
			if (pos < len)
				buf[pos] = ch;
		}

		if (lenp != NULL)
			*lenp = pos;
		if (pos > len)
			return NULL;
		else
			return buf;
	}
	/* NOTREACHED */
}

ssize_t
kvm_write(kvm_t *kd, u_long kva, const void *buf, size_t len)
{
	int cc;

	if (kvm_notrans(kd)) {
		/*
		 * Just like kvm_read, only we write.
		 */
		errno = 0;
		if (lseek(kd->vmfd, (off_t)kva, 0) == -1 && errno != 0) {
			_kvm_err(kd, 0, "invalid address (%lx)", kva);
			return (-1);
		}
		cc = write(kd->vmfd, buf, len);
		if (cc < 0) {
			_kvm_syserr(kd, 0, "kvm_write");
			return (-1);
		} else if (cc < len)
			_kvm_err(kd, kd->program, "short write");
		return (cc);
	} else {
		_kvm_err(kd, kd->program,
		    "kvm_write not implemented for dead kernels");
		return (-1);
	}
	/* NOTREACHED */
}
