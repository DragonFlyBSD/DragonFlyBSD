void test(int (*fn)(), int expected, char *msg, char *msg2);
void test_as_root(int (*fn)(), int expected, char *msg);
void test_as_jailed_root(int (*fn)(), int expected, char *msg);
void test_as_unpriv(int (*fn)(), int expected, char *msg);
void test_as_jailed_unpriv(int (*fn)(), int expected, char *msg);
