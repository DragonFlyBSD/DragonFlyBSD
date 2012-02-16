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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/kthread.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <machine/varargs.h>
#include <sys/module.h>
#include <libprop/proplib.h>
#include <sys/tbridge.h>

static cdev_t		tbridge_dev;
static d_open_t		tbridge_dev_open;
static d_close_t	tbridge_dev_close;
static d_ioctl_t	tbridge_dev_ioctl;


static struct dev_ops tbridge_dev_ops = {
	{ "tbridge", 0, 0 },
	.d_open = tbridge_dev_open,
	.d_close = tbridge_dev_close,
	.d_ioctl = tbridge_dev_ioctl
};

static struct tbridge_testcase *tbridge_curtest = NULL;
static prop_dictionary_t tbridge_testcase = NULL;
static int tbridge_result_ready = 0;
static char tbridge_msgbuf[131072]; /* 128 kB message buffer */
static char *tbridge_msgbuf_ptr;
size_t tbridge_msgbuf_remsz;

static prop_dictionary_t
testcase_get_result_dict(prop_dictionary_t testcase)
{
	prop_dictionary_t result_dict;
	int r;

	result_dict = prop_dictionary_get(testcase, "result");
	if (result_dict == NULL) {
		result_dict = prop_dictionary_create();
		if (result_dict == NULL) {
			kprintf("could not allocate new result dict");
			return NULL;
		}

		r = prop_dictionary_set(testcase, "result", result_dict);
		if (r == 0) {
			kprintf("prop_dictionary operation failed");
			return NULL;
		}
	}

	return result_dict;
}

static int
testcase_get_timeout(prop_dictionary_t testcase)
{
	int32_t val;
	int r;

	r = prop_dictionary_get_int32(prop_dictionary_get(testcase, "opts"),
	    "timeout_in_secs", &val);
	if (r == 0)
		val = 600/* default timeout = 10min */;
		
	return val;
}

static int
testcase_set_stdout_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "stdout_buf", buf);
}

static int
testcase_set_result(prop_dictionary_t testcase, int result)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_int32(dict, "result", result);
}

static const char *
testcase_get_name(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(testcase, "name", &str);
	if (r == 0)
		str = "???";

	return str;
}

int
tbridge_printf(const char *fmt, ...)
{
	__va_list args;
	int i;

	__va_start(args,fmt);
	i = kvsnprintf(tbridge_msgbuf_ptr, tbridge_msgbuf_remsz, fmt, args);
	__va_end(args);

	tbridge_msgbuf_ptr += i;
	tbridge_msgbuf_remsz -= i;

	return i;
}

static int
tbridge_register(struct tbridge_testcase *test)
{
	if (tbridge_curtest != NULL)
		return EBUSY;

	tbridge_curtest = test;

	return 0;
}

static int
tbridge_unregister(void)
{
	tbridge_curtest = NULL;

	return 0;
}

int
tbridge_testcase_modhandler(module_t mod, int type, void *arg)
{
	struct tbridge_testcase *tcase = (struct tbridge_testcase *)arg;
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		error = tbridge_register(tcase);
		break;

	case MOD_UNLOAD:
		if (tcase->tb_abort != NULL)
			error = tcase->tb_abort();

		if (!error)
			tbridge_unregister();
		break;

	default:
		break;
	}

	return error;
}

void
tbridge_test_done(int result)
{
	KKASSERT(tbridge_testcase != NULL);

	kprintf("testcase '%s' called test_done with result=%d\n",
	    testcase_get_name(tbridge_testcase), result);

	testcase_set_result(tbridge_testcase, result);
	testcase_set_stdout_buf(tbridge_testcase, tbridge_msgbuf);

	tbridge_result_ready = 1;
	wakeup(&tbridge_result_ready);
}

static void
tbridge_reset(void)
{
	tbridge_msgbuf_ptr = tbridge_msgbuf;
	tbridge_msgbuf_remsz = sizeof(tbridge_msgbuf);

	if (tbridge_testcase != NULL) {
		prop_object_release(tbridge_testcase);
		tbridge_testcase = NULL;
	}

	safemem_reset_error_count();
	tbridge_result_ready = 0;
}


/*
 * dev stuff
 */
static int
tbridge_dev_open(struct dev_open_args *ap)
{
	return 0;
}

static int
tbridge_dev_close(struct dev_close_args *ap)
{
	return 0;
}

static int
tbridge_dev_ioctl(struct dev_ioctl_args *ap)
{
	struct plistref *pref;
	struct thread *td;
	int error, r;

	error = 0;

	/* Use proplib(3) for userspace/kernel communication */
	pref = (struct plistref *)ap->a_data;

	switch(ap->a_cmd) {
	case TBRIDGE_LOADTEST:
		tbridge_reset();

		if (tbridge_curtest == NULL)
			return EINVAL;

		error = prop_dictionary_copyin_ioctl(pref, ap->a_cmd,
		    &tbridge_testcase);
		if (error)
			return error;

		break;

	case TBRIDGE_GETRESULT:
		error = kthread_create(tbridge_curtest->tb_run, NULL, &td,
		    "testrunner");
		if (error)
			tbridge_test_done(RESULT_PREFAIL);

		/* The following won't be called if the thread wasn't created */
		if (!tbridge_result_ready) {
			r = tsleep(&tbridge_result_ready, 0, "tbridgeres",
			    hz * testcase_get_timeout(tbridge_testcase));
			if (r != 0) {
				if (tbridge_curtest->tb_abort != NULL)
					tbridge_curtest->tb_abort();

				tbridge_test_done(RESULT_TIMEOUT);
			}
		}

		if (safemem_get_error_count() != 0) {
			/*
			 * If there were any double-frees or buffer over- or
			 * underflows, we mark the result as 'signalled', i.e.
			 * the equivalent of a SIGSEGV/SIGBUS in userland.
			 */
			testcase_set_result(tbridge_testcase, RESULT_SIGNALLED);
		}

		error = prop_dictionary_copyout_ioctl(pref, ap->a_cmd,
		    tbridge_testcase);
		if (error)
			return error;

		break;
	default:
		error = ENOTTY; /* Inappropriate ioctl for device */
		break;
	}

	return(error);
}


static int
testbridge_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		tbridge_dev = make_dev(&tbridge_dev_ops,
		    0,
		    UID_ROOT,
		    GID_WHEEL,
		    0600,
		    "tbridge");

		tbridge_testcase = NULL;
		tbridge_reset();

		kprintf("dfregress kernel test bridge ready!\n");
		return 0;

	case MOD_UNLOAD:
		destroy_dev(tbridge_dev);
		kprintf("dfregress kernel test bridge unloaded.\n");
		return 0;
	}

	return EINVAL;
}

static moduledata_t testbridge_mod = {
	"testbridge",
	testbridge_modevent,
	0
};

DECLARE_MODULE(testbridge, testbridge_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(testbridge, 1);
