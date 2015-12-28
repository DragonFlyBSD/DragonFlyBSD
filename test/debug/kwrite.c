/*
 * Tool to read/write kernel memory.
 *
 * Compile with
	cc -o kwrite kwrite.c -lkvm
 *
 * $DragonFly: src/test/debug/kwrite.c,v 1.1 2006/01/09 15:05:45 corecode Exp $
 */

#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>

static void
usage(void)
{
	fprintf(stderr, "usage: kwrite addr[/width] [bval] [bval]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	char errbuf[_POSIX2_LINE_MAX];
	kvm_t *kvm;
	unsigned long addr, width = 1, i;
	char *endptr, *endptr2;

	if (argc < 2)
		usage();

	errno = 0;
	addr = strtoul(argv[1], &endptr, 0);
	if (endptr == argv[1])
		errx(1, "no addr");
	if (errno != 0)
		err(1, "addr '%s'", argv[1]);
	switch (*endptr) {
	case 0:
		break;
	case '/':
		endptr++;
		width = strtoul(endptr, &endptr2, 0);
		if (endptr2 == endptr)
			errx(1, "no width");
		if (errno != 0)
			err(1, "width '%s'", endptr);
		break;
	default:
		errx(1, "invalid addr '%s'", argv[1]);
	}

	kvm = kvm_openfiles(NULL, NULL, NULL, O_RDWR, errbuf);
	if (kvm == NULL)
		errx(1, "%s", errbuf);

	if (argc < 3) {
		for (i = 0; i < width; i++) {
			char b;

			if (kvm_read(kvm, addr + i, &b, 1) != 1)
				errx(1, "%s", kvm_geterr(kvm));
			printf("%#0*lx %02x\n",
				(int)sizeof(addr) * 2, addr + i, b);
		}
	} else {
		if (argc < width + 2)
			errx(1, "too few values for width %ld", width);
		width = argc - 2;
		for (i = 0; i < width; i++) {
			long v;
			char b, bo;

			errno = 0;
			v = strtol(argv[2 + i], &endptr, 0);
			if (endptr == argv[2 + i] || *endptr != 0)
				errx(1, "invalid value '%s'", argv[2 + i]);
			if (errno != 0 || v < 0 || v > 256) {
				if (errno == 0) errno = ERANGE;
				errx(1, "value '%s'", argv[2 + i]);
			}
			b = v;

			if (kvm_read(kvm, addr + i, &bo, 1) != 1)
				errx(1, "%s", kvm_geterr(kvm));
			printf("%#0*lx %02x -> %02x\n",
			       (int)sizeof(addr) * 2, addr + i, bo, b);
			if (kvm_write(kvm, addr + i, &b, 1) != 1)
				errx(1, "%s", kvm_geterr(kvm));
		}
	}

	return 0;
}
