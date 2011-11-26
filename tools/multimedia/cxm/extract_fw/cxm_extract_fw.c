/*-
 * Copyright (c) 2003
 *	John Wehle <john@feith.com>.  All rights reserved.
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
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Conexant MPEG-2 Codec firmware extraction program.
 *
 * Generates:
 *
 * - cxm_dec_fw.c and cxm_enc_fw.c from the
 * Hauppauge PVR-250 / PVR-350 Microsoft Windows driver
 * (i.e. hcwpvrp2.sys).
 *
 * - cxm_cx2584x_fw.c from the Hauppauge PVR-150 / PVR-500
 * Microsoft Windows driver (i.e. HcwMakoC.ROM). (optional)
 *
 * This was written using the invaluable information
 * compiled by The IvyTV Project (ivtv.sourceforge.net).
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const uint8_t decoder_magic[] = {
	0xa7, 0x03, 0x00, 0x00, 0x66, 0xbb, 0x55, 0xaa
};
const uint8_t encoder_magic[] = {
	0xa7, 0x0d, 0x00, 0x00, 0x66, 0xbb, 0x55, 0xaa
};

static int
save_firmware(const char *name, const uint8_t *buf, size_t nbytes)
{
	FILE *ofp;
	char outfile[MAXPATHLEN];
	size_t i;

	if (nbytes > (256 * 1024))
		nbytes = 256 * 1024;

	if ((size_t)snprintf(outfile, sizeof(outfile), "%s.c", name) >=
	    sizeof(outfile))
		errx(1, "save_firmware -- firmware name is too long");

	if (!(ofp = fopen(outfile, "w")))
		err(1, "save_firmware -- can't open output file <%s>",
		    outfile);

	fprintf(ofp, "#include <sys/types.h>\n"
	    "\n"
	    "const uint8_t %s[] __attribute__ ((aligned(4))) = {",
	    name);

	for (i = 0; i < nbytes; i++) {
		if (i)
			fputc(',', ofp);
		if ((i % 8) == 0)
			fputs("\n\t", ofp);
		else
			fputc(' ', ofp);
		fprintf(ofp, "0x%.2x", buf[i]);
	}

	fprintf(ofp, "\n};\n");

	if (ferror(ofp)) {
		fclose(ofp);
		return -1;
	}

	fclose(ofp);
	return 0;
}


int
main(int argc, char **argv)
{
	uint8_t *end;
	uint8_t *ptr;
	uint8_t *start;
	int decoder_fw_saved = 0;
	int encoder_fw_saved = 0;
	int fd, i;
	struct stat statbuf;

	if (argc != 2) {
		fprintf(stderr, "usage: cxm_extract_fw file\n");
		exit(1);
	}

	for (i = 1; i <= (argc - 1); i++) {
		/*
		 * Open the file.
		 */
		if ((fd = open(argv[i], O_RDONLY)) < 0)
			err(1, "can't open %s for reading", argv[i]);

		/*
		 * Determine how big it is.
		 */
		if (fstat(fd, &statbuf) < 0) {
			close(fd);
			err(1, "can't fstat %s", argv[i]);
		}

		/*
		 * Map it into memory.
		 */
		if (!(start = (uint8_t *)mmap(NULL, (size_t) statbuf.st_size,
			    PROT_READ, MAP_SHARED, fd, (off_t) 0))) {
			close(fd);
			err(1, "can't mmap %s", argv[i]);
		}
		end = start + statbuf.st_size;

		close(fd);

		if (statbuf.st_size > 100000) {
			for (ptr = start; ptr != end; ptr++) {
				if ((size_t)(end - ptr) >= sizeof(decoder_magic) &&
				    memcmp(ptr, decoder_magic, sizeof(decoder_magic)) == 0) {
					if (!decoder_fw_saved) {
						if (save_firmware("cxm_dec_fw", ptr, end - ptr) < 0)
							errx(1, "save_firmware failed");
						decoder_fw_saved = 1;
					} else {
						errx(1, "multiple decoder images present");
					}
				}
				if ((size_t)(end - ptr) >= sizeof(encoder_magic) &&
				    memcmp(ptr, encoder_magic, sizeof(encoder_magic)) == 0) {
					if (!encoder_fw_saved) {
						if (save_firmware("cxm_enc_fw", ptr, end - ptr) < 0)
							errx(1, "save_firmware failed");
						encoder_fw_saved = 1;
					} else {
						errx(1, "multiple encoder images present");
					}
				}
			}
		} else {
			errx(1, "save_firmware failed");
		}

		munmap((caddr_t)start, (size_t)statbuf.st_size);

		if (!decoder_fw_saved)
			errx(1, "decoder image not present");

		if (!encoder_fw_saved)
			errx(1, "encoder image not present");

		if (!decoder_fw_saved || !encoder_fw_saved)
			exit(1);
	}

	exit(0);
}
