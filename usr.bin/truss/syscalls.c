/*
 * Copryight 1997 Sean Eric Fagan
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
 *	This product includes software developed by Sean Eric Fagan
 * 4. Neither the name of the author may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.bin/truss/syscalls.c,v 1.10.2.6 2003/04/14 18:24:38 mdodd Exp $
 */

/*
 * This file has routines used to print out system calls and their
 * arguments.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "truss.h"
#include "extern.h"
#include "syscall.h"

/*
 * This should probably be in its own file.
 */

struct syscall syscalls[] = {
	{ "readlink", 1, 3,
	  { { String, 0 } , { String | OUT, 1 }, { Int64, 2 }}},
	{ "lseek", 2, 4,
	  { { Int, 0 }, {Hex64, 2 }, { Int, 3 }}},
	{ "mmap", 2, 6,
	  { { Hex64, 0 }, {Hex64, 1}, {Hex, 2}, {Hex, 3}, {Int, 4}, {Hex64, 5}}},
	{ "open", 1, 3,
	  { { String | IN, 0} , { Hex, 1}, {Octal, 2}}},
	{ "close", 1, 1, { { Int | IN, 0 } } },
	{ "fstat", 1, 2,
	  { { Int, 0},  {Ptr | OUT , 1 }}},
	{ "stat", 1, 2,
	  { { String | IN, 0 }, { Ptr | OUT, 1 }}},
	{ "lstat", 1, 2,
	  { { String | IN, 0 }, { Ptr | OUT, 1 }}},
	{ "write", 1, 3,
	  { { Int, 0}, { Ptr | IN, 1 }, { Int64, 2 }}},
	{ "ioctl", 1, 3,
	  { { Int, 0}, { Ioctl, 1 }, { Hex64, 2 }}},
	{ "execve", 0, 3,
	  { { String | IN, 0} , { Ptr, 1}, {Ptr, 2}}},
	{ "sbrk", 1, 1, { { Hex64, 0 }}},
	{ "exit", 0, 1, { { Int, 0 }}},
	{ "access", 1, 2, { { String | IN, 0 }, { Int, 1 }}},
	{ "chdir", 1, 1, { { String | IN, 0 }}},
	{ "fchdir", 1, 1, { { Int | IN, 0 }}},
	{ "sigaction", 1, 3,
	  { { Signal, 0 }, { Ptr | IN, 1 }, { Ptr | OUT, 2 }}},
	{ "accept", 1, 3,
	  { { Int, 0 }, { Sockaddr | OUT, 1 }, { Ptr | OUT, 2 } } },
	{ "bind", 1, 3,
	  { { Int, 0 }, { Sockaddr | IN, 1 }, { Int, 2 } } },
	{ "connect", 1, 3,
	  { { Int, 0 }, { Sockaddr | IN, 1 }, { Int, 2 } } },
	{ "getpeername", 1, 3,
	  { { Int, 0 }, { Sockaddr | OUT, 1 }, { Ptr | OUT, 2 } } },
	{ "getsockname", 1, 3,
	  { { Int, 0 }, { Sockaddr | OUT, 1 }, { Ptr | OUT, 2 } } },
	{ 0, 0, 0, { { 0, 0 }}},
};

/*
 * If/when the list gets big, it might be desirable to do it
 * as a hash table or binary search.
 */

struct syscall *
get_syscall(const char *name) {
	struct syscall *sc = syscalls;

	while (sc->name) {
		if (!strcmp(name, sc->name))
			return sc;
		sc++;
	}
	return NULL;
}

/*
 * get_struct
 *
 * Copy a fixed amount of bytes from the process.
 */

static int
get_struct(int procfd, off_t offset, void *buf, int len) {
	ssize_t count;

	count = pread(procfd, buf, len, offset);
	if (count != len)
		return -1;
	else
		return 0;
}

/*
 * get_string
 * Copy a C string from the process. The maximum length of the string is
 * max if non-zero, and MAXPATHLEN otherwise.
 * The returned buffer is allocated with malloc(), and needs to be free()'d
 * after use.
 */

char *
get_string(int procfd, off_t offset, size_t max) {
	char *buf, *str;

	max = max ? max : MAXPATHLEN;
	buf = malloc(max);
	ssize_t count;

	count = pread(procfd, buf, max, offset);
	if (count <= 0)
		buf[0] = '\0';

	str = strndup(buf, count);
	free(buf);
	return str;
}


/*
 * print_arg
 * Converts a syscall argument into a string.  Said string is
 * allocated via malloc(), so needs to be free()'d.  The file
 * descriptor is for the process' memory (via /proc), and is used
 * to get any data (where the argument is a pointer).  sc is
 * a pointer to the syscall description (see above); args is
 * an array of all of the system call arguments.
 */

char *
print_arg(int fd, struct syscall_args *sc, unsigned long *args) {
  char *tmp = NULL;
  switch (sc->type & ARG_MASK) {
  case Hex:
    asprintf(&tmp, "0x%x", (uint32_t)args[sc->offset]);
    break;
  case Octal:
    asprintf(&tmp, "0%o", (uint32_t)args[sc->offset]);
    break;
  case Int:
    asprintf(&tmp, "%d", (int32_t)args[sc->offset]);
    break;
  case Hex64:
    asprintf(&tmp, "0x%lx", args[sc->offset]);
    break;
  case Int64:
    asprintf(&tmp, "%ld", args[sc->offset]);
    break;
  case String:
    {
      char *tmp2;
      tmp2 = get_string(fd, (off_t)args[sc->offset], 0);
      asprintf(&tmp, "\"%s\"", tmp2);
      free(tmp2);
    }
  break;
  case Ptr:
    asprintf(&tmp, "0x%lx", args[sc->offset]);
    break;
  case Ioctl:
    {
      const char *temp = ioctlname(args[sc->offset]);
      if (temp)
	tmp = strdup(temp);
      else
	asprintf(&tmp, "0x%lx", args[sc->offset]);
    }
    break;
  case Signal:
    {
      long sig;

      sig = args[sc->offset];
      if (sig > 0 && sig < sys_nsig) {
	int i;
	asprintf(&tmp, "sig%s", sys_signame[sig]);
	for (i = 0; tmp[i] != '\0'; ++i)
	  tmp[i] = toupper(tmp[i]);
      } else {
        asprintf(&tmp, "%ld", sig);
      }
    }
    break;
  case Sockaddr:
    {
      struct sockaddr_storage ss;
      char addr[64];
      struct sockaddr_in *lsin;
      struct sockaddr_in6 *lsin6;
      struct sockaddr_un *sun;
      struct sockaddr *sa;
      char *p;
      u_char *q;
      int i;

      /* yuck: get ss_len */
      if (get_struct(fd, (off_t)args[sc->offset], &ss,
	sizeof(ss.ss_len) + sizeof(ss.ss_family)) == -1)
	err(1, "get_struct %p", (void *)args[sc->offset]);
      /* sockaddr_un never have the length filled in! */
      if (ss.ss_family == AF_UNIX) {
	if (get_struct(fd, (off_t)args[sc->offset], &ss,
	  sizeof(*sun))
	  == -1)
	  err(2, "get_struct %p", (void *)args[sc->offset]);
      } else {
	if (get_struct(fd, (off_t)args[sc->offset], &ss,
	    ss.ss_len < sizeof(ss) ? ss.ss_len : sizeof(ss))
	  == -1)
	  err(2, "get_struct %p", (void *)args[sc->offset]);
      }

      switch (ss.ss_family) {
      case AF_INET:
	lsin = (struct sockaddr_in *)&ss;
	inet_ntop(AF_INET, &lsin->sin_addr, addr, sizeof addr);
	asprintf(&tmp, "{ AF_INET %s:%d }", addr, htons(lsin->sin_port));
	break;
      case AF_INET6:
	lsin6 = (struct sockaddr_in6 *)&ss;
	inet_ntop(AF_INET6, &lsin6->sin6_addr, addr, sizeof addr);
	asprintf(&tmp, "{ AF_INET6 [%s]:%d }", addr, htons(lsin6->sin6_port));
	break;
      case AF_UNIX:
        sun = (struct sockaddr_un *)&ss;
        asprintf(&tmp, "{ AF_UNIX \"%s\" }", sun->sun_path);
	break;
      default:
	sa = (struct sockaddr *)&ss;
        asprintf(&tmp, "{ sa_len = %d, sa_family = %d, sa_data = {%n%*s } }",
	  (int)sa->sa_len, (int)sa->sa_family, &i,
	  6 * (int)(sa->sa_len - ((char *)&sa->sa_data - (char *)sa)), "");
	if (tmp != NULL) {
	  p = tmp + i;
          for (q = (u_char *)&sa->sa_data; q < (u_char *)sa + sa->sa_len; q++)
            p += sprintf(p, " %#02x,", *q);
	}
      }
    }
    break;
  }
  return tmp;
}

/*
 * print_syscall
 * Print (to trussinfo->outfile) the system call and its arguments.  Note that
 * nargs is the number of arguments (not the number of words; this is
 * potentially confusing, I know).
 */

void
print_syscall(struct trussinfo *trussinfo, const char *name, int nargs, char **s_args) {
  int i;
  int len = 0;
  len += fprintf(trussinfo->outfile, "%s(", name);
  for (i = 0; i < nargs; i++) {
    if (s_args[i])
      len += fprintf(trussinfo->outfile, "%s", s_args[i]);
    else
      len += fprintf(trussinfo->outfile, "<missing argument>");
    len += fprintf(trussinfo->outfile, "%s", i < (nargs - 1) ? "," : "");
  }
  len += fprintf(trussinfo->outfile, ")");
  for (i = 0; i < 6 - (len / 8); i++)
	fprintf(trussinfo->outfile, "\t");
}

void
print_syscall_ret(struct trussinfo *trussinfo, const char *name, int nargs, char **s_args, int errorp, int retval) {
  print_syscall(trussinfo, name, nargs, s_args);
  if (errorp) {
    fprintf(trussinfo->outfile, " ERR#%d '%s'\n", retval, strerror(retval));
  } else {
    fprintf(trussinfo->outfile, " = %d (0x%x)\n", retval, retval);
  }
}
