#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libprop/proplib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trivial.h"
#include "plotter.h"

struct ploticus_plot {
	unsigned type;
	prop_dictionary_t params;
	FILE *fp;
	char *path;
};

struct ploticus_plotter {
	int nr_plots;
	struct ploticus_plot **plots;
	const char *basepath;
};

const char *plot_prefabs[] = {
	[PLOT_TYPE_HIST] = "dist",
	[PLOT_TYPE_LINE] = "lines",
};

static
void *
ploticus_init(const char *base)
{
	struct ploticus_plotter *ctx;

	if (!(ctx = calloc(1, sizeof(*ctx))))
		return ctx;
	if (!(ctx->basepath = strdup(base)))
		goto free_ctx;
	return ctx;
free_ctx:
	free(ctx);
	return NULL;
}

static
int
ploticus_new_plot_hist(struct ploticus_plot *plot)
{
	prop_dictionary_t params = plot->params;
	prop_string_t str;

	if (!(str = prop_string_create_cstring_nocopy("1")))
		return !0;
	if (!prop_dictionary_set(params, "x", str)) {
		prop_object_release(str);
		return !0;
	}
	if (!(str = prop_string_create_cstring_nocopy("yes")))
		return !0;
	if (!prop_dictionary_set(params, "curve", str)) {
		prop_object_release(str);
		return !0;
	}
	return 0;
}

static
int
ploticus_new_plot_line(struct ploticus_plot *plot)
{
	prop_dictionary_t params = plot->params;
	prop_string_t str;

	if (!(str = prop_string_create_cstring_nocopy("1")))
		return !0;
	if (!prop_dictionary_set(params, "x", str)) {
		prop_object_release(str);
		return !0;
	}
	if (!(str = prop_string_create_cstring_nocopy("2")))
		return !0;
	if (!prop_dictionary_set(params, "y", str)) {
		prop_object_release(str);
		return !0;
	}
	return 0;
}

int (*plot_type_initializers[])(struct ploticus_plot *) = {
	[PLOT_TYPE_HIST] = ploticus_new_plot_hist,
	[PLOT_TYPE_LINE] = ploticus_new_plot_line,
};

static
plotid_t
ploticus_new_plot(void *_ctx, enum plot_type type, const char *title)
{
	struct ploticus_plot *plot;
	prop_dictionary_t params;
	prop_string_t str;
	struct ploticus_plotter *ctx = _ctx;
	struct ploticus_plot **tmp;
	char *datapath;

	if ((type <= PLOT_TYPE_START) || (type >= PLOT_TYPE_END))
		return -1;
	if (!(tmp = realloc(ctx->plots, sizeof(struct ploticus_plot *) *
			    (ctx->nr_plots + 1))))
		return -1;
	ctx->plots = tmp;

	if (!(params = prop_dictionary_create()))
		return -1;
	if (!(plot = calloc(1, sizeof(*plot))))
		goto free_params;
	plot->params = params;
	plot->type = type;

	if (asprintf(&plot->path, "%s-%d-%s", ctx->basepath, type,
		     title) < 0)
		goto free_plot;
	if (asprintf(&datapath, "%s.data", plot->path) < 0)
		goto free_path;

	if (!(str = prop_string_create_cstring(title)))
		goto free_datapath;
	if (!prop_dictionary_set(params, "title", str)) {
		prop_object_release(str);
		goto free_datapath;
	}
	if (!(str = prop_string_create_cstring(datapath)))
		goto free_datapath;
	if (!prop_dictionary_set(params, "data", str)) {
		prop_object_release(str);
		goto free_datapath;
	}

	if (plot_type_initializers[type](plot))
		goto free_datapath;
	if (!(plot->fp = fopen(datapath, "w"))) {
		goto free_datapath;
	}
	free(datapath);
	ctx->plots[ctx->nr_plots] = plot;
	return ctx->nr_plots++;

free_datapath:
	free(datapath);
free_path:
	free(plot->path);
free_plot:
	free(plot);
free_params:
	prop_object_release(params);
	return -1;
}

static
int
ploticus_plot_histogram(void *_ctx, plotid_t id, double val)
{
	struct ploticus_plotter *ctx = _ctx;
	struct ploticus_plot *plot;

	if ((id < 0) || (id >= ctx->nr_plots))
		return ERANGE;
	plot = ctx->plots[id];
	assert(plot != NULL);

	fprintf(plot->fp, "%lf\n", val);

	return 0;
}

static
int
ploticus_plot_line(void *_ctx, plotid_t id, double x, double y)
{
	struct ploticus_plotter *ctx = _ctx;
	struct ploticus_plot *plot;

	if ((id < 0) || (id >= ctx->nr_plots))
		return ERANGE;
	plot = ctx->plots[id];
	assert(plot != NULL);

	fprintf(plot->fp, "%lf %lf\n", x, y);

	return 0;
}

extern char **environ;

static
void
ploticus_run(struct ploticus_plot *plot)
{
	unsigned nr_params;
	const char **pl_argv;
	prop_object_iterator_t it;
	prop_object_t key, val;
	const char *keystr;
	const char *output_format = "-svg";
	int i;

	printd(PLOT, "ploticus_run\n");
	nr_params = prop_dictionary_count(plot->params);
	if (!(pl_argv = calloc(nr_params +
			       1 +	/* progname */
			       1 +	/* trailing NULL */
			       1 +	/* -prefab */
			       1 +	/* dist */
			       1 +	/* output format */
			       1 +	/* -o */
			       1	/* outpath */
			       , sizeof(char *))))
		err(1, "can't allocate argv");
	if (!(it = prop_dictionary_iterator(plot->params)))
		err(1, "can't allocate dictionary iterator");
	pl_argv[0] = "ploticus";
	pl_argv[1] = "-prefab";
	pl_argv[2] = plot_prefabs[plot->type];
	pl_argv[2 + nr_params + 1] = output_format;
	pl_argv[2 + nr_params + 2] = "-o";
	if (asprintf(__DECONST(char **, &pl_argv[2 + nr_params + 3]),
		     "%s.svg", plot->path) < 0)
		err(1, "Can't allocate args");
	key = prop_object_iterator_next(it);
	for (i = 3; key; ++i, key = prop_object_iterator_next(it)) {
		keystr = prop_dictionary_keysym_cstring_nocopy(key);
		assert(keystr != NULL);
		val = prop_dictionary_get_keysym(plot->params, key);
		assert(val != NULL);
		printd(PLOT, "%s=%s\n", keystr,
		       prop_string_cstring_nocopy(val));
		if (asprintf(__DECONST(char **, &pl_argv[i]), "%s=%s", keystr,
			     prop_string_cstring_nocopy(val)) < 0)
			err(1, "can't allocate exec arguments");
	}
	prop_object_iterator_release(it);
	printd(PLOT, "about to exec with args:\n");
	for (i = 0; pl_argv[i]; ++i)
		printd(PLOT, "%s\n", pl_argv[i]);
	execve("/usr/local/bin/ploticus", __DECONST(char * const *, pl_argv), environ);
	err(1, "failed to exec ploticus");
}

static
int
ploticus_plot_generate(struct ploticus_plot *plot)
{
	pid_t pid;
	int status;

	fclose(plot->fp);

	switch ((pid = fork())) {
	case -1:
		return -1;
	case 0:	/* child */
		ploticus_run(plot);
		assert(!"can't get here");
	}
	/* parent */
	if (waitpid(pid, &status, 0) != pid)
		err(1, "waitpid() failed");
	if (!WIFEXITED(status))
		warn("ploticus did not exit!");
	if (WEXITSTATUS(status))
		warn("ploticus did not run successfully");
	return 0;
}

static
int
ploticus_plot_finish(void *_ctx)
{
	struct ploticus_plotter *ctx = _ctx;
	int i;

	for (i = 0; i < ctx->nr_plots; ++i) {
		if (ploticus_plot_generate(ctx->plots[i]))
			return -1;
	}
	return 0;
}

static struct plotter ploticus_plotter = {
	.plot_init = ploticus_init,
	.plot_new = ploticus_new_plot,
	.plot_histogram = ploticus_plot_histogram,
	.plot_line = ploticus_plot_line,
	.plot_finish = ploticus_plot_finish,
};

static const char *ploticus_path = "/usr/local/bin/ploticus";

struct plotter *
plotter_factory(void)
{
	struct stat st;
	if ((!stat(ploticus_path, &st)) &&
	    S_ISREG(st.st_mode) &&
	    (st.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)))
		return &ploticus_plotter;
	warnx("%s does not exist or is not an executable file", ploticus_path);
	return NULL;
}
