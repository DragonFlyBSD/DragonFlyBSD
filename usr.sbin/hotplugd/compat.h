/* Used by reentrant functions */

struct syslog_data {
	int	log_file;
	int	connected;
	int	opened;
	int	log_stat;
	const char 	*log_tag;
	int 	log_fac;
	int 	log_mask;
};

#define SYSLOG_DATA_INIT {-1, 0, 0, 0, (const char *)0, LOG_USER, 0xff}
#define syslog_r(_a, _b, _c...) syslog(_a, _c)
#define __dead __dead2
