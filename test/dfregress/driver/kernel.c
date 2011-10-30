 /*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/linker.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include <err.h>

#include <libprop/proplib.h>

#include "parser.h"
#include "testcase.h"
#include "runlist.h"
#include "config.h"
#include "kernel.h"
#include "../framework/dfregress.h"
#include "../framework/tbridge.h"


int
run_kernel(const char *kmod, prop_dictionary_t testcase)
{
	char errmsg[1024];
	prop_dictionary_t tkcase = NULL;
	struct stat sb;
	int fd, r, kmod_id, ret = 0;

	if ((r = stat("/dev/tbridge", &sb)) != 0) {
		sprintf(errmsg, "Kernel bridge module probably not loaded: %s",
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);
		testcase_set_result(testcase, RESULT_PREFAIL);

		return -1;
	}

	/* First kldload the testcase */
	kmod_id = kldload(kmod);
	if (kmod_id < 0) {
		sprintf(errmsg, "Could not load testcase kmod %s: %s", kmod,
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);
		testcase_set_result(testcase, RESULT_PREFAIL);

		return -1;
	}

	/* Open control device */
	fd = open("/dev/tbridge", O_RDWR);
	if (fd < 0) {
		sprintf(errmsg, "Could not open kernel bridge interface: %s",
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);
		testcase_set_result(testcase, RESULT_PREFAIL);

		ret = -1;
		goto unload;
	}

	/* Then load the testcase description into the kernel */
	r = prop_dictionary_send_ioctl(testcase, fd, TBRIDGE_LOADTEST);
	if (r < 0) {
		sprintf(errmsg, "sending testcase to kernel failed: %s",
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);
		testcase_set_result(testcase, RESULT_PREFAIL);

		ret = -1;
		goto unload;
	}

	/* Then wait for the result */
	r = prop_dictionary_recv_ioctl(fd, TBRIDGE_GETRESULT, &tkcase);
	if (r < 0) {
		sprintf(errmsg, "receiving test results from kernel failed: %s",
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);
		testcase_set_result(testcase, RESULT_PREFAIL);

		ret = -1;
		goto unload;
	}

	/* Copy over the values of interest */
	testcase_set_result(testcase, testcase_get_result(tkcase));
	testcase_set_stdout_buf(testcase, testcase_get_stdout_buf(tkcase));

	/* Release copy received from kernel */
	prop_object_release(tkcase);

unload:
	r = kldunload(kmod_id);
	if (r < 0) {
		sprintf(errmsg, "kldunload for %s failed: %s", kmod,
		    strerror(errno));
		testcase_set_sys_buf(testcase, errmsg);

		ret = -1;
	}

	return ret;
}
