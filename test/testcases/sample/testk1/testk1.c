#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/tbridge.h>
#include <dfregress.h>

static void
testk1_run(void *arg __unused)
{
	kprintf("testk1_run called!\n");


	tbridge_printf("Starting run!\n");

	tbridge_printf("Pretty much done!\n");

	tbridge_test_done(RESULT_PASS);
}

static struct tbridge_testcase testk1_case = {
	.tb_run		= testk1_run,
	.tb_abort	= NULL
};

TBRIDGE_TESTCASE_MODULE(testk1, &testk1_case);
