
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/kinfo.h>

int
main(int ac, char **av)
{
	struct kinfo_sigtramp tramp;
	int mib[3];
	size_t len;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_SIGTRAMP;

	len = sizeof(tramp);
	if (sysctl(mib, 3, &tramp, &len, NULL, 0) < 0) {
		perror("tramp");
		exit(1);
	}
	printf("TRAMP: %016jx-%016jx\n",
		tramp.ksigtramp_start,
		tramp.ksigtramp_end);
	return 0;
}
