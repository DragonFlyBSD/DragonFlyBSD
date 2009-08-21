/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 */

/* prototypes for functions found in utils.c */

int atoiwi(const char *);
char *ltoa(long);
char *ltoa7(long);
int digits(long);
char *strecpy(char *, const char *);
char **argparse(char *, int *);
long percentages(int cnt, int *out, long *new, long *old, long *diffs);
const char *errmsg(int);
char *format_time(long);
char *format_k(long);
char *format_k2(long);
int string_index(char *string, const char **array);

struct proc;
struct process_select;
struct system_info;
#ifdef ORDER
extern int (*proc_compares[])(const void *, const void *);
extern int compare_cpu(const void *, const void *);
#else
extern int proc_compare(const void *, const void *);
#endif
int proc_owner(int pid);
caddr_t get_process_info(struct system_info *si, struct process_select *sel,
                         int (*compare)(const void *, const void *));
void get_system_info(struct system_info *si);
void quit(int);
