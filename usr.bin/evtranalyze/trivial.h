#ifndef TRIVIAL_H
#define TRIVIAL_H

#define DEFINE_DEBUG_FLAG(nam, chr)\
	nam = 1 << (chr - 'a')

enum debug_flags {
	DEFINE_DEBUG_FLAG(INTV, 'i'),
	DEFINE_DEBUG_FLAG(SVG, 's'),
	DEFINE_DEBUG_FLAG(MISC, 'm'),
	DEFINE_DEBUG_FLAG(PLOT, 'p'),
};

#define printd(subsys, ...)				\
	do {						\
		if (evtranalyze_debug & (subsys)) {	\
			fprintf(stderr, __VA_ARGS__);	\
		}					\
	} while (0)

extern unsigned evtranalyze_debug;

#endif	/* TRIVIAL_H */
