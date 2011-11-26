#include <sys/types.h>
#include <sys/jail.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void setup(); 
void teardown();

uid_t unpriv_uid = 5000;
gid_t unpriv_gid = 5000;

void
test(int (*fn)(), int expected, char *msg, char *msg2)
{
	int retval;

	setup();
	retval = fn();
	teardown();

	printf("%s (%s): ", msg, msg2);

	if (retval == expected) {
		printf("OK\n");
	} else {
		printf("FAILED (was: %d, expected: %d)\n", retval, expected);
	}
	fflush(stdout);
}

void 
test_as_root(int (*fn)(), int expected, char *msg)
{
	if (getuid() != 0) {
		fprintf(stderr, "must be run as root\n");
		exit(-1);
	}

	test(fn, expected, msg, "as root");
}

void 
test_as_jailed_root(int (*fn)(), int expected, char *msg)
{
	if (getuid() != 0) {
		fprintf(stderr, "must be run as root\n");
		exit(-1);
	}

	int child = fork();

	if (child == -1) {
		fprintf(stderr, "fork failed\n");
		exit(-2);
	}

	if (child) {
		struct jail j;
		j.version = 1;
		j.path = "/";
		j.hostname = "jail";
		j.n_ips = 0;

		int jid = jail(&j);
		if (jid < 0) {
			fprintf(stderr, "jail failed\n");
			exit(-1); // TODO
		}
		test(fn, expected, msg, "as jailed root");
		exit(0);
	}
	else {
		waitpid(child, NULL, 0);
	}
}

void 
test_as_unpriv(int (*fn)(), int expected, char *msg)
{
	if (getuid() != 0) {
		fprintf(stderr, "must be run as root\n");
		exit(-1);
	}

	int child = fork();

	if (child == -1) {
		fprintf(stderr, "fork failed\n");
		exit(-2);
	}

	if (child) {
		setgid(unpriv_gid);
		setuid(unpriv_uid);

		if (getuid() != unpriv_uid || getgid() != unpriv_gid) {
			fprintf(stderr, "setuid/gid failed\n");
			exit(-1); // TODO
		}
		test(fn, expected, msg, "as unpriv");
		exit(0);
	}
	else {
		waitpid(child, NULL, 0);
	}
}

void 
test_as_jailed_unpriv(int (*fn)(), int expected, char *msg)
{
	if (getuid() != 0) {
		fprintf(stderr, "must be run as root\n");
		exit(-1);
	}

	int child = fork();

	if (child == -1) {
		fprintf(stderr, "fork failed\n");
		exit(-2);
	}

	if (child) {
		struct jail j;
		j.version = 1;
		j.path = "/";
		j.hostname = "jail";
		j.n_ips = 0;

		int jid = jail(&j);
		if (jid < 0) {
			fprintf(stderr, "jail failed\n");
			exit(-1); // TODO
		}

		setgid(unpriv_gid);
		setuid(unpriv_uid);

		if (getuid() != unpriv_uid || getgid() != unpriv_gid) {
			fprintf(stderr, "setuid/gid failed\n");
			exit(-1); // TODO
		}
		test(fn, expected, msg, "as jailed unpriv");
		exit(0);
	}
	else {
		waitpid(child, NULL, 0);
	}
}
