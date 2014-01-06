#ifndef _EVTR_INTERNAL_H_
#define _EVTR_INTERNAL_H_

#include <sys/queue.h>
#include "evtr.h"

extern unsigned evtr_debug;

#define DEFINE_DEBUG_FLAG(nam, chr)		\
	nam = 1 << (chr - 'a')

enum debug_flags {
	DEFINE_DEBUG_FLAG(IO, 'i'),
	DEFINE_DEBUG_FLAG(DS, 't'),	/* data structures */
	DEFINE_DEBUG_FLAG(LEX, 'l'),
	DEFINE_DEBUG_FLAG(PARSE, 'p'),
	DEFINE_DEBUG_FLAG(MISC, 'm'),
};

#define printd(subsys, ...)				\
	do {						\
		if (evtr_debug & (subsys)) {	\
			fprintf(stderr, "%s:", #subsys);	\
			fprintf(stderr, __VA_ARGS__);	\
		}					\
	} while (0)

#define printw(...)				\
	do {					\
	fprintf(stderr, "Warning: ");		\
	fprintf(stderr, __VA_ARGS__);		\
	} while(0)


struct ktrfmt_parse_ctx {
	struct symtab *symtab;
	struct evtr_variable *var;
	struct evtr_variable_value *val;
	evtr_event_t ev;
	char *errbuf;
	size_t errbufsz;
	int err;
};

TAILQ_HEAD(evtr_value_list, evtr_variable_value);
typedef struct evtr_value_list *evtr_value_list_t;

struct symtab *symtab_new(void);
struct evtr_variable * symtab_find(const struct symtab *, const char *);
int symtab_insert(struct symtab *, const char *, struct evtr_variable *);
void symtab_destroy(struct symtab *);


int parse_string(evtr_event_t, struct symtab *, const char *, char *, size_t);
int parse_var(const char *, struct symtab *, struct evtr_variable **,
	      char *, size_t);

struct hashtab;
struct hashentry;
struct hashtab * hash_new(void);
int hash_find(const struct hashtab *, uintptr_t, uintptr_t *);
struct hashentry * hash_insert(struct hashtab *, uintptr_t, uintptr_t);

void evtr_variable_value_list_destroy(evtr_value_list_t);


#endif /* _EVTR_INTERNAL_H_ */
