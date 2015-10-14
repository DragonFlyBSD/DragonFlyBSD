#include <sys/types.h>
#define _KERNEL_STRUCTURES
#include <sys/file.h>
#include <sys/kinfo.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-N kern] [-M core]\n", cmd);
	exit(1);
}

static const char *
typestr(short type)
{
	static char buf[32];

	switch (type) {
	case DTYPE_VNODE:
		return "file";
	case DTYPE_SOCKET:
		return "socket";
	case DTYPE_PIPE:
		return "pipe";
	case DTYPE_FIFO:
		return "fifo";
	case DTYPE_KQUEUE:
		return "kqueue";
	case DTYPE_CRYPTO:
		return "crypto";
	case DTYPE_SYSLINK:
		return "syslink";
	case DTYPE_MQUEUE:
		return "mqueue";
	default:
		snprintf(buf, sizeof(buf), "?%d", type);
		return buf;
	}
}

int
main(int argc, char *argv[])
{
	const char *file, *core;
	char *errbuf;
	kvm_t *kd;
	int opt, kfile_cnt, i;
	struct kinfo_file *kfile;

	file = NULL;
	core = NULL;

	while ((opt = getopt(argc, argv, "M:N:")) != -1) {
		switch (opt) {
		case 'M':
			core = optarg;
			break;

		case 'N':
			file = optarg;
			break;

		default:
			usage(argv[0]);
		}
	}

	errbuf = malloc(_POSIX2_LINE_MAX);
	if (errbuf == NULL)
		err(2, "malloc %d failed", _POSIX2_LINE_MAX);

	kd = kvm_openfiles(file, core, NULL, O_RDONLY, errbuf);
	if (kd == NULL)
		errx(2, "%s", errbuf);

	kfile = kvm_getfiles(kd, 0, 0, &kfile_cnt);
	if (kfile == NULL)
		errx(2, "kvm_getfiles failed %s", kvm_geterr(kd));

	for (i = 0; i < kfile_cnt; ++i) {
		printf("pid %d, fd %d, type %s\n",
		    kfile[i].f_pid, kfile[i].f_fd, typestr(kfile[i].f_type));
	}

	kvm_close(kd);

	return 0;
}
