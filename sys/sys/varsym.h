/*
 * SYS/VARSYM.H
 *
 *	Implements structures used for variant symlink support.
 * 
 * $DragonFly: src/sys/sys/varsym.h,v 1.3 2005/01/14 02:25:08 joerg Exp $
 */

#ifndef _SYS_VARSYM_H_
#define _SYS_VARSYM_H_

/*#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)  FUTURE */
#if 1

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif

struct varsym {
    int		vs_refs;	/* a lot of sharing occurs */
    int		vs_namelen;
    char	*vs_name;	/* variable name */
    char	*vs_data;	/* variable contents */
};

typedef struct varsym	*varsym_t;

struct varsyment {
    TAILQ_ENTRY(varsyment) ve_entry;
    varsym_t	ve_sym;
};

struct varsymset {
    TAILQ_HEAD(, varsyment) vx_queue;
    int		vx_setsize;
};

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#define VARSYM_PROC	1
#define VARSYM_USER	2
#define VARSYM_SYS	3
#define VARSYM_PRISON	4	/* used internally */

#define VARSYM_PROC_MASK	(1 << VARSYM_PROC)
#define VARSYM_USER_MASK	(1 << VARSYM_USER)
#define VARSYM_SYS_MASK		(1 << VARSYM_SYS)
#define VARSYM_ALL_MASK		(VARSYM_PROC_MASK|VARSYM_USER_MASK|VARSYM_SYS_MASK)

#define MAXVARSYM_NAME	64
#define MAXVARSYM_DATA	256
#define MAXVARSYM_SET	8192

#ifdef _KERNEL

varsym_t varsymfind(int mask, const char *name, int namelen);
int	varsymmake(int level, const char *name, const char *data);
void	varsymdrop(varsym_t var);
void	varsymset_init(struct varsymset *varsymset, struct varsymset *copy);
void	varsymset_clean(struct varsymset *varsymset);
int	varsymreplace(char *cp, int linklen, int maxlen);

#endif	/* _KERNEL */

#endif
