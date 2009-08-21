/*
 *
 */

void show_help(void);
char *next_field(char *str);
int scanint(char *str, int *intp);

const char *kill_procs(char *);
const char *renice_procs(char *str);

int error_count(void);
void show_errors(void);
