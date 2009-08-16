#ifndef DEVFSCTL_H
#define DEVFSCTL_H

#include <sys/queue.h>

#define iswhitespace(X)	((((X) == ' ') || ((X) == '\t'))?1:0)
#define RULE_MAX_STACK	32


struct groupdevid {
	enum {
		isGROUP = 1,
		isNAME,
		isTYPE
	}		type;

	union {
		char		*name;
		int		devtype;
	};

	struct groupdevid **list;
	size_t	listsize;
#if 0
    struct groupdevid *next;
	TAILQ_HEAD(, groupdevid) list;
#endif
	TAILQ_ENTRY(groupdevid) link;
};

struct rule {
	enum {
		rPERM = 1,
		rLINK,
		rHIDE,
		rSHOW
	}		type;

	struct groupdevid *id;
	char		*dest;
	uid_t		uid;
	uid_t		gid;
	int		mode;
	int		jail;

	TAILQ_ENTRY(rule) link;
};

typedef int (rule_iterate_callback_t)(struct rule *rule,
		struct groupdevid *id);
typedef int (rule_parser_t)(char **);

struct groupdevid *new_id(const char *, int);
struct groupdevid *get_id(const char *);
struct groupdevid *get_group(const char *, int);
struct rule *new_rule(int, struct groupdevid *);
void add_rule(struct rule *);
int rule_apply(void);
void dump_config(void);
int read_config(const char *);
int syntax_error(const char *fmt, ...);
#endif
