#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NETPERF_CMD	"netperf"
#define NETPERF_PATH	"/usr/local/bin/" NETPERF_CMD

#define TCP_STRM_FILENAME	"/tmp/tcp_strm.%d.%d"

struct netperf_child {
	int		fd;
};

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -H host [-l len_s] [-i instances] [-r|-s]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct netperf_child *instance;
	char len_str[32];
	char *args[32];
	const char *host;
	volatile int ninst;
	int len, ninst_done;
	int opt, i, null_fd, set_minmax = 0;
	volatile int reverse = 0, sfile = 0;
	double result, res_max, res_min, jain;
	pid_t mypid;

	host = NULL;
	ninst = 2;
	len = 10;

	while ((opt = getopt(argc, argv, "i:H:l:rs")) != -1) {
		switch (opt) {
		case 'i':
			ninst = strtoul(optarg, NULL, 10);
			break;

		case 'H':
			host = optarg;
			break;

		case 'l':
			len = strtoul(optarg, NULL, 10);
			break;

		case 'r':
			reverse = 1;
			sfile = 0;
			break;

		case 's':
			reverse = 0;
			sfile = 1;
			break;

		default:
			usage(argv[0]);
		}
	}
	if (ninst <= 0 || host == NULL || len <= 0)
		usage(argv[0]);

	mypid = getpid();

	snprintf(len_str, sizeof(len_str), "%d", len);

	i = 0;
	args[i++] = __DECONST(char *, NETPERF_CMD);
	args[i++] = __DECONST(char *, "-P0");
	args[i++] = __DECONST(char *, "-H");
	args[i++] = __DECONST(char *, host);
	args[i++] = __DECONST(char *, "-l");
	args[i++] = __DECONST(char *, len_str);
	args[i++] = __DECONST(char *, "-t");
	if (reverse)
		args[i++] = __DECONST(char *, "TCP_MAERTS");
	else if (sfile)
		args[i++] = __DECONST(char *, "TCP_SENDFILE");
	else
		args[i++] = __DECONST(char *, "TCP_STREAM");
	args[i] = NULL;

	instance = calloc(ninst, sizeof(struct netperf_child));
	if (instance == NULL) {
		fprintf(stderr, "calloc failed\n");
		exit(1);
	}

	null_fd = open("/dev/null", O_RDWR);
	if (null_fd < 0) {
		fprintf(stderr, "open null failed: %d\n", errno);
		exit(1);
	}

	for (i = 0; i < ninst; ++i) {
		char filename[128];

		snprintf(filename, sizeof(filename), TCP_STRM_FILENAME,
		    (int)mypid, i);
		instance[i].fd = open(filename, O_CREAT | O_TRUNC | O_RDWR,
		    S_IWUSR | S_IRUSR);
		if (instance[i].fd < 0) {
			fprintf(stderr, "open %s failed: %d\n",
			    filename, errno);
			exit(1);
		}
	}

	for (i = 0; i < ninst; ++i) {
		pid_t pid;

		pid = vfork();
		if (pid == 0) {
			int ret;

			dup2(instance[i].fd, STDOUT_FILENO);
			dup2(null_fd, STDERR_FILENO);
			ret = execv(NETPERF_PATH, args);
			if (ret < 0) {
				fprintf(stderr, "execv %d failed: %d\n",
				    i, errno);
				_exit(1);
			}
			/* Never reached */
			abort();
		} else if (pid < 0) {
			fprintf(stderr, "vfork %d failed: %d\n", i, errno);
			exit(1);
		}
	}

	ninst_done = 0;
	while (ninst_done < ninst) {
		pid_t pid;

		pid = waitpid(-1, NULL, 0);
		if (pid < 0) {
			fprintf(stderr, "waitpid failed: %d\n", errno);
			exit(1);
		}
		++ninst_done;
	}

	res_max = 0.0;
	res_min = 0.0;
	jain = 0.0;
	result = 0.0;
	for (i = 0; i < ninst; ++i) {
		char line[128], filename[128];
		FILE *fp;

		close(instance[i].fd);
		snprintf(filename, sizeof(filename), TCP_STRM_FILENAME,
		    (int)mypid, i);
		fp = fopen(filename, "r");
		if (fp == NULL) {
			fprintf(stderr, "fopen %s failed\n", filename);
			exit(1);
		}

		while (fgets(line, sizeof(line), fp) != NULL) {
			int n, arg1, arg2, arg3;
			double res, arg4;

			n = sscanf(line, "%d%d%d%lf%lf",
			    &arg1, &arg2, &arg3, &arg4, &res);
			if (n == 5) {
				if (!set_minmax) {
					res_max = res;
					res_min = res;
					set_minmax = 1;
				} else {
					if (res > res_max)
						res_max = res;
					if (res < res_min)
						res_min = res;
				}
				jain += (res * res);
				result += res;
				break;
			}
		}
		fclose(fp);
		unlink(filename);
	}

	jain *= ninst;
	jain = (result * result) / jain;

	printf("%s %.2f Mbps\n", reverse ? "TCP_MAERTS" : "TCP_STREAM", result);
	printf("min/max (jain) %.2f Mbps/%.2f Mbps (%f)\n",
	    res_min, res_max, jain);

	exit(0);
}
