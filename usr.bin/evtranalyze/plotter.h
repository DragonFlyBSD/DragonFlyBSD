#ifndef _PLOTTER_H_
#define _PLOTTER_H_

typedef int plotid_t;
enum plot_type {
	PLOT_TYPE_START,
	PLOT_TYPE_HIST,
	PLOT_TYPE_LINE,
	PLOT_TYPE_END
};

struct plotter {
	void *(*plot_init)(const char *);
	plotid_t (*plot_new)(void *, enum plot_type, const char *);
	int (*plot_histogram)(void *, plotid_t, double);
	int (*plot_line)(void *, plotid_t, double, double);
	int (*plot_finish)(void *);
};

struct plotter *plotter_factory(void);

#endif /* _PLOTTER_H_ */
