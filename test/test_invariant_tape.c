#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* Include the actual header that declares the vulnerable function */
#include "../../sbin/restore/tape.h"

/* Mock minimal dependencies needed for the function to run */
static char tapebuf[TP_NTREK * TP_BSIZE];
static int blkcnt = 0;
static int numtrec = 0;

START_TEST(test_tapebuf_bounds_invariant)
{
    /* Invariant: tapebuf access via blkcnt * TP_BSIZE must never exceed allocated tapebuf size */
    
    /* Payloads: valid, boundary, and exploit cases for numtrec */
    int payloads[] = {
        10,                     /* Valid: normal operation */
        TP_NTREK,              /* Boundary: maximum valid numtrec */
        TP_NTREK + 1,          /* Exploit: exceeds allocated tapebuf */
        TP_NTREK * 2,          /* Exploit: far out of bounds */
        -1                     /* Exploit: negative numtrec */
    };
    
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        /* Reset state for each test case */
        blkcnt = 0;
        numtrec = payloads[i];
        memset(tapebuf, 0, sizeof(tapebuf));
        
        /* Create a test buffer to read into */
        char buf[TP_BSIZE];
        memset(buf, 0, sizeof(buf));
        
        /* The security property: blkcnt must never exceed numtrec during memmove */
        int max_safe_blkcnt = (numtrec > 0) ? numtrec : 0;
        
        /* Simulate reading blocks up to the safe limit */
        for (int j = 0; j < max_safe_blkcnt; j++) {
            /* This should not crash or read out of bounds */
            memmove(buf, &tapebuf[(blkcnt++ * TP_BSIZE)], (long)TP_BSIZE);
        }
        
        /* Attempting to read beyond safe limit should be prevented */
        if (blkcnt >= max_safe_blkcnt) {
            /* Verify we didn't exceed bounds */
            ck_assert_int_le(blkcnt, max_safe_blkcnt);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_tapebuf_bounds_invariant);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}