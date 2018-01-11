/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include "defs.h"

static void usage(const char *av0);
static void dotest(const char *target);
static void add_server(const char *target);
static void process_config_file(const char *path);
static pid_t check_pid(void);
static void set_pid(const char *av0);
static void sigint_handler(int signo __unused);

static struct server_info **servers;
static int nservers;
static int maxservers;

int daemon_opt = 1;
int debug_opt = 0;
int debug_level = -1;		/* (set to default later) */
int quickset_opt = 0;		/* immediately set time of day on startup */
int no_update_opt = 0;		/* do not make any actual updates */
int min_sleep_opt = 5;		/* 5 seconds minimum poll interval */
int nom_sleep_opt = 300;	/* 5 minutes nominal poll interval */
int max_sleep_opt = 1800;	/* 30 minutes maximum poll interval */
int family = PF_UNSPEC;		/* Address family */
double insane_deviation = 0.5;	/* 0.5 seconds of deviation == insane */
const char *config_opt;		/* config file */
const char *pid_opt = "/var/run/dntpd.pid";

int
main(int ac, char **av)
{
    int test_opt = 0;
    pid_t pid;
    int ch;
    int i;

    /*
     * Really randomize
     */
    srandomdev();

    /*
     * Process Options
     */
    while ((ch = getopt(ac, av, "46df:i:l:np:qstFL:QST:")) != -1) {
	switch(ch) {
	case '4':
	    family = PF_INET;
	    break;
	case '6':
	    family = PF_INET6;
	    break;
	case 'd':
	    debug_opt = 1;
	    daemon_opt = 0;
	    if (debug_level < 0)
		debug_level = 99;
	    if (config_opt == NULL)
		config_opt = "/dev/null";
	    break;
	case 'p':
	    pid_opt = optarg;
	    break;
	case 'f':
	    config_opt = optarg;
	    break;
	case 'i':
	    insane_deviation = strtod(optarg, NULL);
	    break;
	case 'l':
	    debug_level = strtol(optarg, NULL, 0);
	    break;
	case 'n':
	    no_update_opt = 1;
	    break;
	case 'q':
	    debug_level = 0;
	    break;
	case 's':
	    quickset_opt = 1;
	    break;
	case 'S':
	    quickset_opt = 0;
	    break;
	case 't':
	    test_opt = 1;
	    debug_opt = 1;
	    daemon_opt = 0;
	    if (debug_level < 0)
		debug_level = 99;
	    if (config_opt == NULL)
		config_opt = "/dev/null";
	    break;
	case 'F':
	    daemon_opt = 0;
	    break;
	case 'L':
	    max_sleep_opt = strtol(optarg, NULL, 0);
	    break;
	case 'T':
	    nom_sleep_opt = strtol(optarg, NULL, 0);
	    if (nom_sleep_opt < 1) {
		fprintf(stderr, "Warning: nominal poll interval too small, "
				"limiting to 1 second\n");
		nom_sleep_opt = 1;
	    }
	    if (nom_sleep_opt > 24 * 60 * 60) {
		fprintf(stderr, "Warning: nominal poll interval too large, "
				"limiting to 24 hours\n");
		nom_sleep_opt = 24 * 60 * 60;
	    }
	    if (min_sleep_opt > nom_sleep_opt)
		min_sleep_opt = nom_sleep_opt;
	    if (max_sleep_opt < nom_sleep_opt * 5)
		max_sleep_opt = nom_sleep_opt * 5;
	    break;
	case 'Q':
	    if ((pid = check_pid()) != 0) {
		fprintf(stderr, "%s: killing old daemon\n", av[0]);
		kill(pid, SIGINT);
		usleep(100000);
		if (check_pid())
		    sleep(1);
		if (check_pid())
		    sleep(9);
		if (check_pid()) {
		    fprintf(stderr, "%s: Unable to kill running daemon.\n", av[0]);
		} else {
		    fprintf(stderr, "%s: Running daemon has been terminated.\n", av[0]);
		}
	    } else {
		fprintf(stderr, "%s: There is no daemon running to kill.\n", av[0]);
	    }
	    exit(0);
	    break;
	case 'h':
	default:
	    usage(av[0]);
	    /* not reached */
	}
    }

    /*
     * Make sure min and nom intervals are less then or equal to the maximum
     * interval.
     */
    if (min_sleep_opt > max_sleep_opt)
	min_sleep_opt = max_sleep_opt;
    if (nom_sleep_opt > max_sleep_opt)
	nom_sleep_opt = max_sleep_opt;

    /*
     * Set default config file
     */
    if (config_opt == NULL) {
	if (optind != ac)
	    config_opt = "/dev/null";
	else
	    config_opt = "/etc/dntpd.conf";
    }

    if (debug_level < 0)
	debug_level = 1;

    process_config_file(config_opt);

    if (debug_opt == 0)
	openlog("dntpd", LOG_CONS|LOG_PID, LOG_DAEMON);

    if (test_opt) {
	if (optind != ac - 1)
	    usage(av[0]);
	dotest(av[optind]);
	/* not reached */
    }

    /*
     * Add additional hosts.
     */
    for (i = optind; i < ac; ++i) {
	add_server(av[i]);
    }
    if (nservers == 0) {
	usage(av[0]);
	/* not reached */
    }

    /*
     * Do an initial course time setting if requested using the first
     * host successfully polled.
     */
    /* XXX */

    /*
     * Daemonize, stop logging to stderr.
     */
    if (daemon_opt) {
	if ((pid = check_pid()) != 0) {
	    logerrstr("%s: NOTE: killing old daemon and starting a new one", 
			av[0]);
	    kill(pid, SIGINT);
	    usleep(100000);
	    if (check_pid())
		sleep(1);
	    if (check_pid())
		sleep(9);
	    if (check_pid()) {
		logerrstr("%s: Unable to kill running daemon, exiting", av[0]);
		exit(1);
	    }
	}
	daemon(0, 0);
    } else if (check_pid() != 0) {
	logerrstr("%s: A background dntpd is running, you must kill it first",
		av[0]);
	exit(1);
    }
    if (debug_opt == 0) {
	log_stderr = 0;
	set_pid(av[0]);
	signal(SIGINT, sigint_handler);
	logdebug(0, "dntpd version %s started\n", DNTPD_VERSION);
    }

    /*
     * And go.
     */
    sysntp_clear_alternative_corrections();
    client_init();
    client_check_duplicate_ips(servers, nservers);
    client_main(servers, nservers);
    return(0);
}

static
void
usage(const char *av0)
{
    fprintf(stderr, "%s [-dnqstFSQ] [-f config_file] [-l log_level] [-T poll_interval] [-L poll_limit] [additional_targets]\n", av0);
    fprintf(stderr, 
	"\t-d\tDebugging mode, implies -F, -l 99, and logs to stderr\n"
	"\t-f file\tSpecify the config file (/etc/dntpd.conf)\n"
	"\t-l int\tSet log level (0-4), default 1\n"
	"\t-n\tNo-update mode.  No offset or frequency corrections are made\n"
	"\t-q\tQuiet-mode, same as -L 0\n"
	"\t-s\tSet the time immediately on startup\n"
	"\t-t\tTest mode, implies -F, -l 99, -n, logs to stderr\n"
	"\t-F\tRun in foreground (log still goes to syslog)\n"
	"\t-L int\tMaximum polling interval\n"
	"\t-S\tDo not set the time immediately on startup\n"
	"\t-T int\tNominal polling interval\n"
	"\t-Q\tTerminate any running background daemon\n"
	"\n"
	"\t\tNOTE: in debug and test modes -f must be specified if\n"
	"\t\tyou want to use a config file.\n"
    );
    exit(1);
}

static
void
dotest(const char *target)
{
    struct server_info info;

    bzero(&info, sizeof(info));
    info.sam = (struct sockaddr *)&info.sam_st;
    info.fd = udp_socket(target, 123, info.sam, LOG_DNS_ERROR);
    if (info.fd < 0) {
	logerrstr("unable to create UDP socket for %s", target);
	return;
    }
    info.target = strdup(target);
    client_init();

    fprintf(stderr, 
	    "Will run %d-second polls until interrupted.\n", nom_sleep_opt);

    for (;;) {
	client_poll(&info, nom_sleep_opt, 1);
	sleep(nom_sleep_opt);
    }
    /* not reached */
}

static char *
myaddr2ascii(struct sockaddr *sa)
{
	static char str[INET6_ADDRSTRLEN];
	struct sockaddr_in *soin;
	struct sockaddr_in6 *sin6;

	switch (sa->sa_family) {
	case AF_INET:
		soin = (struct sockaddr_in *) sa;
		inet_ntop(AF_INET, &soin->sin_addr, str, sizeof(str));
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *) sa;
		inet_ntop(AF_INET6, &sin6->sin6_addr, str, sizeof(str));
		break;
	}
	return (str);
}

static void
add_server(const char *target)
{
    server_info_t info;

    if (nservers == maxservers) {
	maxservers += 16;
	servers = realloc(servers, maxservers * sizeof(server_info_t));
	assert(servers != NULL);
    }
    info = malloc(sizeof(struct server_info));
    servers[nservers] = info;
    bzero(info, sizeof(struct server_info));
    info->sam = (struct sockaddr *)&info->sam_st;
    info->target = strdup(target);
    /*
     * Postpone socket opening and server name resolution until we are in main
     * loop to avoid hang during init if network down.
     */
    info->fd = -1;
    info->server_state = -1;
    ++nservers;
}

void
disconnect_server(server_info_t info)
{
    if (info->fd >= 0)
	close(info->fd);
    info->fd = -1;
    if (info->ipstr) {
	free(info->ipstr);
	info->ipstr = NULL;
    }
}

void
reconnect_server(server_info_t info)
{
    const char *ipstr;
    dns_error_policy_t policy;

    /* 
     * Ignore DNS errors if never connected before to handle the case where
     * we're started before network up.
     */
    policy = IGNORE_DNS_ERROR;
    if (info->fd >= 0) {
	close(info->fd);
	policy = LOG_DNS_ERROR;
    }
    if (info->ipstr) {
	free(info->ipstr);
	info->ipstr = NULL;
    }
    info->sam = (struct sockaddr *)&info->sam_st;
    info->fd = udp_socket(info->target, 123, info->sam, policy);
    if (info->fd >= 0) {
	ipstr = myaddr2ascii(info->sam);
	info->ipstr = strdup(ipstr);
    }
}

static void
process_config_file(const char *path)
{
    const char *ws = " \t\r\n";
    char buf[1024];
    char *keyword;
    char *data;
    int line;
    FILE *fi;

    if ((fi = fopen(path, "r")) != NULL) {
	line = 1;
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    if (strchr(buf, '#'))
		*strchr(buf, '#') = 0;
	    if ((keyword = strtok(buf, ws)) != NULL) {
		data = strtok(NULL, ws);
		if (strcmp(keyword, "server") == 0) {
		    if (data == NULL) {
			logerr("%s:%d server missing host specification",
				path, line);
		    } else {
			add_server(data);
		    }
		} else {
		    logerr("%s:%d unknown keyword %s", path, line, keyword);
		}
	    }
	    ++line;
	}
	fclose(fi);
    } else {
	logerr("Unable to open %s", path);
	exit(1);
    }
}

static 
pid_t
check_pid(void)
{
    char buf[32];
    pid_t pid;
    FILE *fi;

    pid = 0;
    if ((fi = fopen(pid_opt, "r")) != NULL) {
	if (fgets(buf, sizeof(buf), fi) != NULL) {
	    pid = strtol(buf, NULL, 0);
	    if (kill(pid, 0) != 0)
		pid = 0;
	}
	fclose(fi);
    }
    return(pid);
}

static 
void
set_pid(const char *av0)
{
    pid_t pid;
    FILE *fo;

    pid = getpid();
    if ((fo = fopen(pid_opt, "w")) != NULL) {
	fprintf(fo, "%d\n", (int)pid);
	fclose(fo);
    } else {
	logerr("%s: Unable to create %s, continuing anyway.", av0, pid_opt);
    }
}

static
void
sigint_handler(int signo __unused)
{
    remove(pid_opt);
    /* dangerous, but we are exiting anyway so pray... */
    logdebug(0, "dntpd version %s stopped\n", DNTPD_VERSION);
    exit(0);
}

