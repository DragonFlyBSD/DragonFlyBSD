/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resident.h>
#include <sys/sysctl.h>

#include <machine/elf.h>
#include <a.out.h>
#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void
usage(void)
{
	fprintf(stderr, "usage: resident [-l] [-f] [-x id] [-d] [-R] program ...\n");
	exit(1);
}

static int
list_residents(void)
{
	const char *mib = "vm.resident";
	struct xresident *buf;
	size_t res_count, res_total, i;
	int error;

	/* get the number of resident binaries */
	error = sysctlbyname(mib, NULL, &res_count, NULL, 0);
	if (error != 0) {
		perror("sysctl: vm.resident");
		goto done;
	}

	if (res_count == 0) {
		printf("no resident binaries to list\n");
		goto done;
	}

	/* allocate memory for the list of binaries */
	res_total = sizeof(*buf) * res_count;
	if ((buf = malloc(res_total)) != NULL) {
		/* retrieve entries via sysctl */
		error = sysctlbyname(mib, buf, &res_total, NULL, 0);
		if (error != 0) {
			perror("sysctl: vm.resident");
			goto done;
		}
	} else {
		perror("malloc");
		goto done;
	}
	
	/* print the list of retrieved resident binary */
	printf("%-4s\t%-15s\t%-12s\t%-30s\n","Id", "Size", "Address", "Executable");
	for (i = 0; i < res_count; ++i) {
		printf("%-4d\t%-15jd\t0x%-12x\t%-30s\n",
			buf[i].res_id,
			(intmax_t)buf[i].res_stat.st_size,
			(int)buf[i].res_entry_addr,
			buf[i].res_file);
	}

	/* free back the memory */
	free(buf);

done:
	return error;
}

int
main(int argc, char *argv[])
{
	int	rval;
	int	c;
	int	doreg = 1;
	int	force = 0;

	while ((c = getopt(argc, argv, "Rdflx:")) != -1) {
		switch (c) {
		case 'f':
			force = 1;
			break;
		case 'l':
			rval = list_residents();
			if (rval < 0)
				exit(EXIT_FAILURE);
			else
				exit(EXIT_SUCCESS);
		case 'd':
			doreg = 0;
			break;
		case 'x':
		case 'R':
			if (c == 'R')
			    c = exec_sys_unregister(-2);
			else
			    c = exec_sys_unregister(strtol(optarg, NULL, 0));
			if (c < 0)
			    printf("unregister: %s\n", strerror(errno));
			else
			    printf("unregister: success\n");
			exit(0);
		default:
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		usage();
		/*NOTREACHED*/
	}

	/* ld-elf.so magic */
	if (doreg) {
	    if (setenv("LD_RESIDENT_REGISTER_NOW", "yes", 1) == -1)
			err(1, "setenv failed");
	} else {
	    if (setenv("LD_RESIDENT_UNREGISTER_NOW", "yes", 1) == -1)
			err(1, "setenv failed");
	}

	rval = 0;
	for ( ;  argc > 0;  argc--, argv++) {
		int	fd;
		union {
			struct exec aout;
			Elf_Ehdr elf;
		} hdr;
		int	n;
		int	status;
		int	file_ok;
		int	is_shlib;

		if (force)
			goto force_target;

		if ((fd = open(*argv, O_RDONLY, 0)) < 0) {
			warn("%s", *argv);
			rval |= 1;
			continue;
		}
		if ((n = read(fd, &hdr, sizeof hdr)) == -1) {
			warn("%s: can't read program header", *argv);
			close(fd);
			rval |= 1;
			continue;
		}

		file_ok = 1;
		is_shlib = 0;
		if ((size_t)n >= sizeof hdr.aout && !N_BADMAG(hdr.aout)) {
			/* a.out file */
			if ((N_GETFLAG(hdr.aout) & EX_DPMASK) != EX_DYNAMIC
#if 1 /* Compatibility */
			    || hdr.aout.a_entry < __LDPGSZ
#endif
				) {
				warnx("%s: not a dynamic executable", *argv);
				file_ok = 0;
			}
		} else if ((size_t)n >= sizeof hdr.elf && IS_ELF(hdr.elf)) {
			Elf_Ehdr ehdr;
			Elf_Phdr phdr;
			int dynamic = 0, i;

			if (lseek(fd, 0, SEEK_SET) == -1 ||
			    read(fd, &ehdr, sizeof ehdr) != sizeof ehdr ||
			    lseek(fd, ehdr.e_phoff, SEEK_SET) == -1
			   ) {
				warnx("%s: can't read program header", *argv);
				file_ok = 0;
			} else {
				for (i = 0; i < ehdr.e_phnum; i++) {
					if (read(fd, &phdr, ehdr.e_phentsize)
					   != sizeof phdr) {
						warnx("%s: can't read program header",
						    *argv);
						file_ok = 0;
						break;
					}
					if (phdr.p_type == PT_DYNAMIC)
						dynamic = 1;
				}
			}
			if (!dynamic) {
				warnx("%s: not a dynamic executable", *argv);
				file_ok = 0;
			} else if (hdr.elf.e_type == ET_DYN) {
				if (hdr.elf.e_ident[EI_OSABI] & ELFOSABI_FREEBSD) {
					is_shlib = 1;
				} else {
					warnx("%s: not a FreeBSD ELF shared "
					      "object", *argv);
					file_ok = 0;
				}
			}
		} else {
			warnx("%s: not a dynamic executable", *argv);
			file_ok = 0;
		}
		close(fd);
		if (!file_ok) {
			rval |= 1;
			continue;
		}

		if (is_shlib) {
			rval |= 1;
			warnx("%s: resident not supported on shared libraries.", *argv);
			continue;
		}

force_target:
		fflush(stdout);

		switch (fork()) {
		case -1:
			err(1, "fork");
			break;
		default:
			if (wait(&status) <= 0) {
				warn("wait");
				rval |= 1;
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr, "%s: signal %d\n",
						*argv, WTERMSIG(status));
				rval |= 1;
			} else if (WIFEXITED(status) && WEXITSTATUS(status)) {
				switch(WEXITSTATUS(status)) {
				case ENOENT:
				    fprintf(stderr, "%s: entry not found\n",
					*argv);
				    break;
				case EEXIST:
				    fprintf(stderr, "%s: binary already resident\n",
					*argv);
				    break;
				default:
				    fprintf(stderr, "%s: exit status %s\n",
						*argv, strerror(WEXITSTATUS(status)));
				}
				rval |= 1;
			} else {
			}
			break;
		case 0:
			execl(*argv, *argv, NULL);
			warn("%s", *argv);
			_exit(1);
		}
	}

	return rval;
}
