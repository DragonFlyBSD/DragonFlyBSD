/*-
 * Copyright (c) 2000 Doug Rabson
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
 *
 * $FreeBSD: head/stand/efi/loader/efi_main.c 350654 2019-08-06 19:27:27Z tsoome $
 */

#include <efi.h>
#include <eficonsctl.h>
#include <efilib.h>
#include "loader_efi.h"

static EFI_PHYSICAL_ADDRESS heap;
static UINTN heapsize;

void
efi_exit(EFI_STATUS exit_code)
{

	BS->FreePages(heap, EFI_SIZE_TO_PAGES(heapsize));
	BS->Exit(IH, exit_code, 0, NULL);
}

void
exit(int status)
{

	efi_exit(EFI_LOAD_ERROR);
}

static CHAR16 *
arg_skipsep(CHAR16 *argp)
{

	while (*argp == ' ' || *argp == '\t' || *argp == '\n')
		argp++;
	return (argp);
}

static CHAR16 *
arg_skipword(CHAR16 *argp)
{

	while (*argp && *argp != ' ' && *argp != '\t' && *argp != '\n')
		argp++;
	return (argp);
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
	static EFI_GUID image_protocol = LOADED_IMAGE_PROTOCOL;
	static EFI_GUID console_control_protocol =
	    EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
	EFI_CONSOLE_CONTROL_PROTOCOL *console_control = NULL;
	EFI_LOADED_IMAGE *img;
	CHAR16 *args, **argv, *program;
	EFI_STATUS status;
	int argc;

	IH = image_handle;
	ST = system_table;
	BS = ST->BootServices;
	RS = ST->RuntimeServices;

	status = BS->LocateProtocol(&console_control_protocol, NULL,
	    (VOID **)&console_control);
	if (status == EFI_SUCCESS)
		(void)console_control->SetMode(console_control,
		    EfiConsoleControlScreenText);

#if 1
	heapsize = 3 * 1024 * 1024;
#else /* __FreeBSD__ */
	heapsize = 64 * 1024 * 1024;
#endif
	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(heapsize), &heap);
	if (status != EFI_SUCCESS)
		BS->Exit(IH, status, 0, NULL);

	setheap((void *)(uintptr_t)heap, (void *)(uintptr_t)(heap + heapsize));

	/* Use efi_exit() from here on... */

	status = BS->HandleProtocol(IH, &image_protocol, (VOID**)&img);
	if (status != EFI_SUCCESS)
		efi_exit(status);

	/* Preserve the firmware entry's existing program-name detection. */
	program = (CHAR16 *)L"loader.efi";
	if (img->LoadOptionsSize > 0 && img->LoadOptions != NULL &&
	    img->ParentHandle != NULL &&
	    img->FilePath != NULL &&
	    DevicePathType(img->FilePath) == MEDIA_DEVICE_PATH &&
	    DevicePathSubType(img->FilePath) == MEDIA_FILEPATH_DP &&
	    DevicePathNodeLength(img->FilePath) > sizeof(FILEPATH_DEVICE_PATH))
		program = NULL;
	if (efi_get_args(img->LoadOptions, img->LoadOptionsSize, program,
	    &argc, &argv, &args) != 0)
		efi_exit(EFI_OUT_OF_RESOURCES);

	status = main(argc, argv);
	efi_exit(status);
	return (status);
}

/* Share the existing LoadOptions conversion and tokenization. */
int
efi_get_args(const void *options, size_t size, CHAR16 *program,
    int *count, CHAR16 ***vector, CHAR16 **storage)
{
	CHAR16 *argp, *args, **argv;
	int argc;

	/*
	 * Pre-process the (optional) load options. If the option string
	 * is given as an ASCII string, we use a poor man's ASCII to
	 * Unicode-16 translation. The size of the option string as given
	 * to us includes the terminating null character. We assume the
	 * string is an ASCII string if strlen() plus the terminating
	 * '\0' is less than LoadOptionsSize. Even if all Unicode-16
	 * characters have the upper 8 bits non-zero, the terminating
	 * null character will cause a one-off.
	 * If the string is already in Unicode-16, we make a copy so that
	 * we know we can always modify the string.
	 */
	if (size > 0 && options != NULL) {
		if (size == strlen(options) + 1) {
			args = malloc(size << 1);
			if (args == NULL)
				return (ENOMEM);
			for (argc = 0; argc < (int)size; argc++)
				args[argc] = ((char*)options)[argc];
		} else {
			args = malloc(size);
			if (args == NULL)
				return (ENOMEM);
			memcpy(args, options, size);
		}
	} else
		args = NULL;

	/*
	 * Use a quick and dirty algorithm to build the argv vector. We
	 * first count the number of words. Then, after allocating the
	 * vector, we split the string up. We don't deal with quotes or
	 * other more advanced shell features.
	 * The EFI shell will pass the name of the image as the first
	 * word in the argument list. This does not happen if we're
	 * loaded by the boot manager. This is not so easy to figure
	 * out though. The ParentHandle is not always NULL, because
	 * there can be a function (=image) that will perform the task
	 * for the boot manager.
	 */
	/* Part 2: count words. */
	argc = program != NULL ? 1 : 0;
	argp = args;
	while (argp != NULL && *argp != 0) {
		argp = arg_skipsep(argp);
		if (*argp == 0)
			break;
		argc++;
		argp = arg_skipword(argp);
	}
	/* Part 3: build vector. */
	argv = malloc((argc + 1) * sizeof(CHAR16*));
	if (argv == NULL) {
		free(args);
		return (ENOMEM);
	}
	argc = 0;
	if (program != NULL)
		argv[argc++] = program;
	argp = args;
	while (argp != NULL && *argp != 0) {
		argp = arg_skipsep(argp);
		if (*argp == 0)
			break;
		argv[argc++] = argp;
		argp = arg_skipword(argp);
		/* Terminate the words. */
		if (*argp != 0)
			*argp++ = 0;
	}
	argv[argc] = NULL;

	*count = argc;
	*vector = argv;
	*storage = args;
	return (0);
}
