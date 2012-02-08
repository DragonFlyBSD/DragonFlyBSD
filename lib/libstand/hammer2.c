#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uuid.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <hammer2/hammer2_disk.h>

struct hammer2 {
	int 				fd;	/* Device fd */
	struct hammer2_blockref		sroot;	/* Superroot blockref */
};

struct inode {
	struct hammer2_inode_data	dat;	/* raw inode data */
	off_t				doff;	/* disk inode offset */
};

off_t blockoff(ref)
	struct hammer2_blockref ref;
{

}

hinit(hfs)
	struct hammer2 *hfs;
{
	struct hammer2_volume_data volhdr;
	ssize_t rc;
	hammer2_crc_t crc0;

	rc = pread(hfs->fd, &volhdr, HAMMER2_VOLUME_SIZE, 0);
	if (volhdr.magic == HAMMER2_VOLUME_ID_HBO) {
		printf("Valid HAMMER2 filesystem\n");
	} else {
		return (-1);
	}

	hfs->sroot = volhdr.sroot_blockref;
	return (0);
}

shread(hfs, ino, buf, off, len)
	struct hammer2 *hfs;
	struct inode *ino;
	char *buf;
	off_t off;
	size_t len;
{
	/*
	 * Read [off, off+len) from inode ino rather than from disk
	 * offsets; correctly decodes blockrefs/indirs/...
	 */
}

struct inode *hlookup1(hfs, ino, name)
	struct hammer2 *hfs;
	struct inode *ino;
	char *name;
{
	static struct inode filino;
	off_t off;
	int rc;

	bzero(&filino, sizeof(struct inode));

	for (off = 0;
	     off < ino->dat.size;
	     off += sizeof(struct hammer2_inode_data))
	{
		rc = shread(hfs, ino, &filino.dat, off,
			    sizeof(struct hammer2_inode_data));
		if (rc != sizeof(struct hammer2_inode_data))
			continue;
		if (strcmp(name, &filino.dat.filename) == 0)
			return (&filino);
	}

	return (NULL);
}

struct inode *hlookup(hfs, name)
	struct hammer2 *hfs;
	char *name;
{
	/* Name is of form /SUPERROOT/a/b/c/file */

}

void hstat(hfs, ino, sb)
	struct hammer2 *hfs;
	struct inode *ino;
	struct stat *sb;
{

}

main(argc, argv)
	int argc;
	char *argv[];
{
	struct hammer2 hammer2;
	struct inode *ino;
	struct stat sb;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: hammer2 <dev>\n");
		exit(1);
	}

	hammer2.fd = open(argv[1], O_RDONLY);
	if (hammer2.fd < 0) {
		fprintf(stderr, "unable to open %s\n", argv[1]);
		exit(1);
	}

	if (hinit(&hammer2)) {
		fprintf(stderr, "invalid fs\n");
		close(hammer2.fd);
		exit(1);
	}

	for (i = 2; i < argc; i++) {
		ino = hlookup(&hammer2, argv[i]);
		if (ino == NULL) {
			fprintf(stderr, "hlookup %s\n", argv[i]);
			continue;
		}
		hstat(&hammer2, ino, &sb);

		printf("%s %lld", argv[i], sb.st_size);

	}
}
