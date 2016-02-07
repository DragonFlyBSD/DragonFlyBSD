#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NETPERF_CMD	"netperf"
#define NETPERF_PATH	"/usr/local/bin/" NETPERF_CMD

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

struct netperf_child {
	int		pipes[2];
};

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s -H host [-H host1] [-l len_s] [-i instances] "
	    "[-m msgsz] [-S sockbuf] [-r|-s] [-x]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct netperf_child *instance;
	char len_str[32], sockbuf_str[64], msgsz_str[64];
	char *args[32];
	const char *msgsz, *sockbuf;
	const char **host;
	volatile int ninst, set_minmax = 0, ninstance, nhost, dual;
	int len, ninst_done, host_idx, host_arg_idx, test_arg_idx;
	int opt, i, null_fd;
	volatile int reverse = 0, sfile = 0;
	double result, res_max, res_min, jain;

	dual = 0;
	ninst = 2;
	len = 10;
	msgsz = NULL;
	sockbuf = NULL;

	host_idx = 0;
	nhost = 8;
	host = malloc(sizeof(const char *) * nhost);
	if (host == NULL)
		err(1, "malloc failed");

	while ((opt = getopt(argc, argv, "H:S:i:l:m:rsx")) != -1) {
		switch (opt) {
		case 'H':
			if (host_idx == nhost) {
				const char **new_host;

				nhost *= 2;
				new_host = malloc(sizeof(const char *) * nhost);
				if (new_host == NULL)
					err(1, "malloc failed");
				memcpy(new_host, host,
				    host_idx * sizeof(const char *));
				free(host);
				host = new_host;
			}
			host[host_idx++] = optarg;
			break;

		case 'S':
			sockbuf = optarg;
			break;

		case 'i':
			ninst = strtoul(optarg, NULL, 10);
			break;

		case 'l':
			len = strtoul(optarg, NULL, 10);
			break;

		case 'm':
			msgsz = optarg;
			break;

		case 'r':
			reverse = 1;
			sfile = 0;
			break;

		case 's':
			reverse = 0;
			sfile = 1;
			break;

		case 'x':
			dual = 1;
			break;

		default:
			usage(argv[0]);
		}
	}
	nhost = host_idx;

	if (ninst <= 0 || nhost == 0 || len <= 0)
		usage(argv[0]);

	snprintf(len_str, sizeof(len_str), "%d", len);

	i = 0;
	args[i++] = __DECONST(char *, NETPERF_CMD);
	args[i++] = __DECONST(char *, "-P0");
	args[i++] = __DECONST(char *, "-H");
	host_arg_idx = i;
	args[i++] = __DECONST(char *, NULL);
	args[i++] = __DECONST(char *, "-l");
	args[i++] = __DECONST(char *, len_str);
	args[i++] = __DECONST(char *, "-t");
	test_arg_idx = i;
	if (reverse)
		args[i++] = __DECONST(char *, "TCP_MAERTS");
	else if (sfile)
		args[i++] = __DECONST(char *, "TCP_SENDFILE");
	else
		args[i++] = __DECONST(char *, "TCP_STREAM");
	if (msgsz != NULL || sockbuf != NULL) {
		args[i++] = __DECONST(char *, "--");
		if (msgsz != NULL) {
			snprintf(msgsz_str, sizeof(msgsz_str), "%s,%s",
			    msgsz, msgsz);
			args[i++] = __DECONST(char *, "-m");
			args[i++] = __DECONST(char *, msgsz_str);
			args[i++] = __DECONST(char *, "-M");
			args[i++] = __DECONST(char *, msgsz_str);
		}
		if (sockbuf != NULL) {
			snprintf(sockbuf_str, sizeof(sockbuf_str), "%s,%s",
			    sockbuf, sockbuf);
			args[i++] = __DECONST(char *, "-s");
			args[i++] = __DECONST(char *, sockbuf_str);
			args[i++] = __DECONST(char *, "-S");
			args[i++] = __DECONST(char *, sockbuf_str);
		}
	}
	args[i] = NULL;

	ninstance = ninst * nhost * (dual + 1);
	instance = calloc(ninstance, sizeof(struct netperf_child));
	if (instance == NULL)
		err(1, "calloc failed");

	null_fd = open("/dev/null", O_RDWR);
	if (null_fd < 0)
		err(1, "open null failed");

	for (i = 0; i < ninstance; ++i) {
		if (pipe(instance[i].pipes) < 0)
			err(1, "pipe %dth failed", i);
	}

	for (i = 0; i < ninstance; ++i) {
		pid_t pid;

		pid = vfork();
		if (pid == 0) {
			int ret;

			dup2(instance[i].pipes[1], STDOUT_FILENO);
			dup2(null_fd, STDERR_FILENO);

			args[host_arg_idx] = __DECONST(char *,
			    host[i % nhost]);
			if (dual) {
				const char *test_type;

				if ((i / nhost) & dual) {
					test_type = sfile ?
					    "TCP_SENDFILE" : "TCP_STREAM";
				} else {
					test_type = "TCP_MAERTS";
				}
				args[test_arg_idx] = __DECONST(char *,
				    test_type);
			}
			ret = execv(NETPERF_PATH, args);
			if (ret < 0) {
				warn("execv %d failed", i);
				_exit(1);
			}
			/* Never reached */
			abort();
		} else if (pid < 0) {
			err(1, "vfork %d failed", i);
		}
		close(instance[i].pipes[1]);
		instance[i].pipes[1] = -1;
	}

	ninst_done = 0;
	while (ninst_done < ninstance) {
		pid_t pid;

		pid = waitpid(-1, NULL, 0);
		if (pid < 0)
			err(1, "waitpid failed");
		++ninst_done;
	}

	res_max = 0.0;
	res_min = 0.0;
	jain = 0.0;
	result = 0.0;
	for (i = 0; i < ninstance; ++i) {
		char line[128];
		FILE *fp;

		fp = fdopen(instance[i].pipes[0], "r");
		if (fp == NULL)
			err(1, "fdopen %dth failed", i);

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
	}

	jain *= ninstance;
	jain = (result * result) / jain;

	printf("%s%s %.2f Mbps\n",
	    (dual || reverse) ? "TCP_MAERTS" : "TCP_STREAM",
	    dual ? (sfile ? "/TCP_SENDFILE" : "/TCP_STREAM") : "", result);
	printf("min/max (jain) %.2f Mbps/%.2f Mbps (%f)\n",
	    res_min, res_max, jain);

	exit(0);
}
