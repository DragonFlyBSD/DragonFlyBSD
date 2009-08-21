/* constants needed for display.c */

/* "type" argument for new_message function */

#define  MT_standout  1
#define  MT_delayed   2

struct statics;
struct system_info;

int display_resize(void);
int display_init(struct statics *statics);
void i_loadave(int mpid, double *avenrun);
void u_loadave(int mpid, double *avenrun);
void i_timeofday(time_t *tod);
void i_procstates(int total, int *brkdn);
void u_procstates(int total, int *brkdn);
void u_process(int xline, char *xnewline);
void i_cpustates(struct system_info *si);
void z_cpustates(struct system_info *si);
void i_memory(int *stats);
void    u_memory(int *stats);
void i_swap(int *stats);
void u_swap(int *stats);
void i_message(void);
void u_message(void);
void i_header(char *text);
void u_header(char *text __unused);
void i_process(int line, char *thisline);
void u_endscreen(int hi);
void i_uptime(struct timeval *bt, time_t *tod);
void display_header(int t);
void new_message(int type, const char *msgfmt, ...);
void clear_message(void);
int readline(char *buffer, int size, int numeric);
