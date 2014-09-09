#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	mainloop(const struct sockaddr_in *, long, u_long *);

static int	global_stopped;

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -4 inet4 -p port "
	    "[-i n_instance] [-l duration]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in in;
	int opt, ninst, i;
	long dur;
	u_long *result, sum;
	size_t prm_len;

	prm_len = sizeof(ninst);
	if (sysctlbyname("hw.ncpu", &ninst, &prm_len, NULL, 0) != 0)
		err(2, "sysctl hw.ncpu failed");

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;

	dur = 10;

	while ((opt = getopt(argc, argv, "4:p:i:l:")) != -1) {
		switch (opt) {
		case '4':
			if (inet_pton(AF_INET, optarg, &in.sin_addr) <= 0) {
				fprintf(stderr, "invalid inet address %s\n",
				    optarg);
				usage(argv[0]);
			}
			break;

		case 'p':
			in.sin_port = htons(atoi(optarg));
			break;

		case 'i':
			ninst = atoi(optarg);
			break;

		case 'l':
			dur = strtol(optarg, NULL, 10);
			break;

		default:
			usage(argv[0]);
		}
	}

	if (ninst < 1 || dur < 1 ||
	    in.sin_port == 0 || in.sin_addr.s_addr == INADDR_ANY)
		usage(argv[0]);

	result = mmap(NULL, ninst * sizeof(u_long), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	if (result == MAP_FAILED)
		err(1, "mmap failed");
	memset(result, 0, ninst * sizeof(u_long));

	for (i = 0; i < ninst; ++i) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			mainloop(&in, dur, &result[i]);
			exit(0);
		} else if (pid < 0) {
			err(1, "fork failed");
		}
	}

	for (i = 0; i < ninst; ++i) {
		pid_t pid;

		pid = waitpid(-1, NULL, 0);
		if (pid < 0)
			err(1, "waitpid failed");
	}

	sum = 0;
	for (i = 0; i < ninst; ++i)
		sum += result[i];
	printf("%.2f\n", (double)sum / (double)dur);

	exit(0);
}

static void
signal_handler(int signum __unused)
{
	global_stopped = 1;
}

static void
mainloop(const struct sockaddr_in *in, long dur, u_long *res)
{
	struct itimerval it;
	u_long count = 0;

	if (signal(SIGALRM, signal_handler) == SIG_ERR)
		err(1, "signal failed");

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = dur;
	it.it_value.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &it, NULL) < 0)
		err(1, "setitimer failed");

	while (!global_stopped) {
		int s;

		s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0)
			continue;

		if (connect(s,
		    (const struct sockaddr *)in, sizeof(*in)) == 0)
			++count;
		close(s);
	}
	*res = count;
}
