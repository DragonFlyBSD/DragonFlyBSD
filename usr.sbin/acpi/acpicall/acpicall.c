/*-
 *   Copyright (C) 2011 by Maxim Ignatenko
 *   gelraen.ua@gmail.com
 *   Copyright (C) 2015  Sascha Wildner
 *   swildner@dragonflybsd.org
 *
 *   All rights reserved.                                                  *
 *                                                                         *
 *   Redistribution and use in source and binary forms, with or without    *
 *    modification, are permitted provided that the following conditions   *
 *    are met:                                                             *
 *     * Redistributions of source code must retain the above copyright    *
 *       notice, this list of conditions and the following disclaimer.     *
 *     * Redistributions in binary form must reproduce the above copyright *
 *       notice, this list of conditions and the following disclaimer in   *
 *       the documentation and/or other materials provided with the        *
 *       distribution.                                                     *
 *                                                                         *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   *
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     *
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR *
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  *
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, *
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT      *
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, *
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY *
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT   *
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE *
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  *
 *
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <uuid.h>

#include <contrib/dev/acpica/source/include/acpi.h>
#include <dev/acpica/acpiio_mcall.h>

#define	MAX_ACPI_PATH	4096

static char	dev_path[MAXPATHLEN] = "/dev/acpi";
static char	method_path[MAX_ACPI_PATH] = "";
static size_t	result_buf_size = 1024;
static char	output_format = 'o';
static int	verbose;
static ACPI_OBJECT args[ACPI_METHOD_NUM_ARGS];
static struct acpi_mcall_ioctl_arg params;

static void	usage(void);
static int	parse_buffer(ACPI_OBJECT *, char *);
static void	print_params(struct acpi_mcall_ioctl_arg *);
static void	print_acpi_object(ACPI_OBJECT *);
static void	print_acpi_buffer(ACPI_BUFFER *, char);

int
main(int argc, char *argv[])
{
	char c;
	int i, fd, status;
	uuid_t uuid;

	bzero(&params, sizeof(params));
	params.path = method_path;
	params.args.Count = 0;
	params.args.Pointer = args;

	while ((c = getopt(argc, argv, "b:d:i:o:s:U:v")) != -1) {
		switch (c) {
		case 'b':
		case 'i':
		case 's':
		case 'U':
			i = params.args.Count;
			if (i >= ACPI_METHOD_NUM_ARGS) {
				fprintf(stderr,
				    "maximum number of %d args exceeded\n",
				    ACPI_METHOD_NUM_ARGS);
				exit(1);
			}
			switch (optopt) {
			case 'b':
				if (parse_buffer(&args[i], optarg) != 0) {
					fprintf(stderr,
					    "unable to parse hexstring: %s\n",
					    optarg);
					exit(1);
				}
				break;
			case 'i':
				args[i].Type = ACPI_TYPE_INTEGER;
				args[i].Integer.Value =
				    strtol(optarg, NULL, 10);
				break;
			case 's':
				args[i].Type = ACPI_TYPE_STRING;
				args[i].String.Length = strlen(optarg);
				args[i].String.Pointer = optarg;
				break;
			case 'U':
				uuid_from_string(optarg, &uuid, &status);
				if (status != uuid_s_ok) {
					fprintf(stderr, "invalid uuid %s\n",
					    optarg);
					exit(1);
				}
				args[i].Type = ACPI_TYPE_BUFFER;
				args[i].Buffer.Length = 16;
				if ((args[i].Buffer.Pointer = malloc(16)) == NULL) {
					fprintf(stderr, "malloc failure\n");
					exit(1);
				}
				uuid_enc_le(args[i].Buffer.Pointer, &uuid);
				break;
			}
			params.args.Count++;
			break;
		case 'd':
			strlcpy(dev_path, optarg, MAXPATHLEN);
			break;
		case 'o':
			switch (optarg[0]) {
			case 'b':
			case 'i':
			case 'o':
			case 's':
				output_format = optarg[0];
				break;
			default:
				fprintf(stderr,
				    "incorrect output format: %c\n",
				    optarg[0]);
				usage();
				break;
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();
	strlcpy(method_path, argv[0], MAX_ACPI_PATH);

	params.result.Length = result_buf_size;
	params.result.Pointer = malloc(result_buf_size);

	if (params.result.Pointer == NULL) {
		perror("malloc");
		return 1;
	}

	if (method_path[0] == 0) {
		fprintf(stderr,
		    "please specify path to method with -p flag\n");
		return 1;
	}

	if (verbose)
		print_params(&params);

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (ioctl(fd, ACPIIO_DO_MCALL, &params) == -1) {
		perror("ioctl");
		return 1;
	}

	if (verbose)
		printf("status: %d\nresult: ", params.retval);
	print_acpi_buffer(&params.result, output_format);
	printf("\n");

	return params.retval;
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: acpicall [-v] [-b hexstring] [-d file] [-i number] "
	    "[-o i | s | b | o]\n");
	fprintf(stderr, "                [-s string] [-U uuid] path\n");
	exit(1);
}

static int
parse_buffer(ACPI_OBJECT *dst, char *src)
{
	char tmp[3] = { 0 };
	size_t len = strlen(src) / 2, i;

	dst->Type = ACPI_TYPE_BUFFER;
	dst->Buffer.Length = len;
	if ((dst->Buffer.Pointer = malloc(len)) == NULL) {
		fprintf(stderr,
		    "%s: failed to allocate %zu bytes\n", __func__, len);
		exit(1);
	}

	for (i = 0; i < len; i++) {
		tmp[0] = src[i * 2];
		tmp[1] = src[i * 2 + 1];
		dst->Buffer.Pointer[i] = strtol(tmp, NULL, 16);
	}

	return 0;
}

static void
print_params(struct acpi_mcall_ioctl_arg *p)
{
	int i;

	printf("path: %s\n", p->path);
	printf("number of arguments: %d\n", p->args.Count);
	for (i = 0; i < (int)p->args.Count; i++) {
		printf("argument %d type: ", i + 1);
		switch (p->args.Pointer[i].Type) {
		case ACPI_TYPE_INTEGER:
			printf("integer\n");
			break;
		case ACPI_TYPE_STRING:
			printf("string\n");
			break;
		case ACPI_TYPE_BUFFER:
			printf("buffer\n");
			break;
		}
		printf("argument %d value: ", i + 1);
		print_acpi_object(&(p->args.Pointer[i]));
		printf("\n");
	}
}

static void
print_acpi_object(ACPI_OBJECT *obj)
{
	int i;

	switch (obj->Type) {
	case ACPI_TYPE_INTEGER:
		printf("%ju", (uintmax_t)obj->Integer.Value);
		break;
	case ACPI_TYPE_STRING:
		printf("%s", obj->String.Pointer);
		break;
	case ACPI_TYPE_BUFFER:
		for (i = 0; i < (int)obj->Buffer.Length; i++)
			printf("%02x", obj->Buffer.Pointer[i]);
		break;
	default:
		printf("unknown object type '%d'", obj->Type);
		break;
	}	
}

static void
print_acpi_buffer(ACPI_BUFFER *buf, char format)
{
	int i;

	switch (format) {
	case 'i':
		printf("%ju", (uintmax_t)*((UINT64 *)buf->Pointer));
		break;
	case 's':
		printf("%s", (char *)buf->Pointer);
		break;
	case 'b':
		for (i = 0; i < (int)buf->Length; i++)
			printf("%02x", ((UINT8 *)(buf->Pointer))[i]);
		break;
	case 'o':
		print_acpi_object((ACPI_OBJECT *)buf->Pointer);
		break;
	}
}
